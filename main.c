#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <time.h>
#include <ctype.h>
#include <netdb.h>
#include <curl/curl.h>
#include "memory_forth.h"
#include "forth_bot.h"

Env *head = NULL;
int irc_socket = -1;
char *channel = NULL;
pthread_t main_thread;
pthread_t irc_sender_thread_id;
volatile sig_atomic_t shutdown_flag = 0;
pthread_mutex_t irc_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t env_rwlock = PTHREAD_RWLOCK_INITIALIZER;

void handle_sigusr1(int sig __attribute__((unused))) {
    shutdown_flag = 1;
}

void init_globals(void) {
    head = NULL;
    irc_socket = -1;
    channel = NULL;
    shutdown_flag = 0;
    irc_msg_queue_head = 0;
    irc_msg_queue_tail = 0;
    memset(irc_msg_queue, 0, sizeof(irc_msg_queue));
    for (int i = 0; i < IRC_MSG_QUEUE_SIZE; i++) {
        irc_msg_queue[i].used = 0;
        irc_msg_queue[i].msg[0] = '\0';
    }
}

/*
 * Libère proprement tous les environnements encore vivants.
 * Appelé à l'arrêt du bot (commande EXIT ou signal).
 */
static void free_all_envs(void) {
    /* BUG 8 fix : sauvegarder curr->next AVANT freeEnv() qui libere curr.
     * L'ancienne version lisait curr->next apres freeEnv() — use-after-free.
     * On tient le rdlock uniquement le temps de lire next, avant la liberation. */
    pthread_rwlock_rdlock(&env_rwlock);
    Env *curr = head;
    pthread_rwlock_unlock(&env_rwlock);

    while (curr) {
        char nick_copy[MAX_STRING_SIZE];
        strncpy(nick_copy, curr->nick, sizeof(nick_copy) - 1);
        nick_copy[sizeof(nick_copy) - 1] = '\0';

        /* Lire next avant que freeEnv() libere curr */
        pthread_rwlock_rdlock(&env_rwlock);
        Env *next = curr->next;
        pthread_rwlock_unlock(&env_rwlock);

        char quit_msg[512];
        snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", nick_copy);
        send_to_channel(quit_msg);
        freeEnv(nick_copy);

        curr = next;
    }
}

int main(int argc, char *argv[]) {
    char *server_name = "labynet.fr";
    char bot_nick[512] = "forth";
    char default_channel[] = "#labynet";

    init_globals();
    main_thread = pthread_self();

    /* --- Gestion des signaux --- */
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    /* --- Arguments --- */
    if (argc == 4) {
        server_name = argv[1];
        strncpy(bot_nick, argv[2], sizeof(bot_nick) - 1);
        bot_nick[sizeof(bot_nick) - 1] = '\0';
        channel = strdup(argv[3]);
    } else if (argc == 1) {
        channel = strdup(default_channel);
    } else {
        printf("Usage: %s [server nick channel]\n", argv[0]);
        return 1;
    }

    if (!channel) {
        fprintf(stderr, "Failed to allocate memory for channel\n");
        return 1;
    }

    printf("Starting Forth IRC bot on %s with nick %s in channel %s\n",
           server_name, bot_nick, channel);

    /*
     * CORRECTION (point 9) : le thread sender est créé UNE SEULE FOIS ici,
     * avant la boucle de reconnexion. Il survit aux déconnexions/reconnexions
     * et s'arrête proprement quand shutdown_flag passe à 1.
     */
    if (pthread_create(&irc_sender_thread_id, NULL, irc_sender_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create IRC sender thread\n");
        free(channel);
        return 1;
    }

    /* --- Boucle principale de connexion/reconnexion --- */
    int reconnect_delay = 5;

    while (!shutdown_flag) {

        /* ---- Phase de (re)connexion ---- */
        if (irc_socket == -1) {
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family   = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            int status = getaddrinfo(server_name, "6667", &hints, &res);
            if (status != 0) {
                printf("getaddrinfo failed: %s, retrying in %ds...\n",
                       gai_strerror(status), reconnect_delay);
                sleep(reconnect_delay);
                reconnect_delay = reconnect_delay * 2 > 60 ? 60 : reconnect_delay * 2;
                continue;
            }

            char server_ip[INET_ADDRSTRLEN];
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
            inet_ntop(AF_INET, &(ipv4->sin_addr), server_ip, sizeof(server_ip));
            printf("Connecting to %s with nick %s...\n", server_ip, bot_nick);
            irc_connect(server_ip, bot_nick);
            freeaddrinfo(res);

            if (irc_socket == -1) {
                printf("Connection failed, retrying in %ds...\n", reconnect_delay);
                sleep(reconnect_delay);
                reconnect_delay = reconnect_delay * 2 > 60 ? 60 : reconnect_delay * 2;
                continue;
            }

            printf("Connected to %s\n", server_ip);
            reconnect_delay = 5;

            /*
             * Vider la queue IRC après reconnexion pour ne pas envoyer
             * au nouveau serveur des messages destinés à l'ancienne session.
             */
            pthread_mutex_lock(&irc_msg_queue_mutex);
            irc_msg_queue_head = irc_msg_queue_tail;
            pthread_mutex_unlock(&irc_msg_queue_mutex);
        }

        /* ---- Phase de réception ---- */
        char buffer[4096];
        size_t buffer_pos = 0;
        int registered = 0;

        while (irc_socket != -1 && !shutdown_flag) {

            if (irc_receive(buffer, sizeof(buffer), &buffer_pos) == -1) {
                printf("Disconnected from IRC, closing socket...\n");
                close(irc_socket);
                irc_socket = -1;
                break;
            }

            char *start = buffer;
            char *end;
            while ((end = strstr(start, "\r\n")) != NULL && !shutdown_flag) {
                *end = '\0';
                char *line = start;

                char nick[MAX_STRING_SIZE];
                char cmd[512];

                if (irc_handle_message(line, bot_nick, &registered, nick, cmd, sizeof(cmd))) {

                    if (strcmp(cmd, "QUIT") == 0) {
                        Env *env = findEnv(nick);
                        if (env) {
                            pthread_mutex_lock(&env->queue_mutex);
                            if (env->queue_head != env->queue_tail) {
                                /* Commandes en attente : refuser le QUIT */
                                char quit_msg[512];
                                snprintf(quit_msg, sizeof(quit_msg),
                                         "Operation impossible %s, try later (commands pending)",
                                         nick);
                                pthread_mutex_unlock(&env->queue_mutex);
                                send_to_channel(quit_msg);
                            } else {
                                pthread_mutex_unlock(&env->queue_mutex);
                                freeEnv(nick);
                                char quit_msg[512];
                                snprintf(quit_msg, sizeof(quit_msg),
                                         "Environment for %s has been freed.", nick);
                                send_to_channel(quit_msg);
                            }
                        } else {
                            send_to_channel("No environment found for you to quit.");
                        }

                    } else if (strcmp(cmd, "EXIT") == 0) {
                        send_to_channel("Bot shutting down...");
                        free_all_envs();
                        shutdown_flag = 1;
                        pthread_cond_broadcast(&irc_msg_queue_cond);
                        break;

                    } else {
                        enqueue(cmd, nick);
                    }
                }

                start = end + 2;
            }

            /* Compacter le buffer : déplacer les données non traitées au début */
            if (start != buffer) {
                size_t remaining = buffer_pos - (size_t)(start - buffer);
                memmove(buffer, start, remaining);
                buffer_pos = remaining;
                buffer[buffer_pos] = '\0';
            } else if (buffer_pos == sizeof(buffer) - 1) {
                printf("Buffer full, resetting...\n");
                send_to_channel("Warning: IRC buffer full, data may be lost");
                buffer_pos = 0;
                buffer[0] = '\0';
            }
        }
    }

    /* --- Arrêt propre --- */

    if (irc_socket != -1) {
        send_to_channel("Bot shutting down due to signal...");
        close(irc_socket);
        irc_socket = -1;
    }

    /* Libérer les environnements restants (cas arrêt par signal) */
    free_all_envs();

    /*
     * Réveiller le sender thread pour qu'il voie shutdown_flag et se termine,
     * puis attendre sa fin.
     */
    pthread_cond_broadcast(&irc_msg_queue_cond);
    pthread_join(irc_sender_thread_id, NULL);

    if (channel) {
        free(channel);
        channel = NULL;
    }

    printf("Bot terminated cleanly.\n");
    return 0;
}
