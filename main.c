#include <pthread.h>
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
char *channel;
pthread_mutex_t irc_mutex = PTHREAD_MUTEX_INITIALIZER;

void enqueue(const char *cmd, const char *nick) {
    Env *env = findEnv(nick);
    if (!env) {
        env = createEnv(nick);
        if (!env) return;
    }

    pthread_mutex_lock(&env->queue_mutex);
    if ((env->queue_tail + 1) % QUEUE_SIZE != env->queue_head) {
        strncpy(env->queue[env->queue_tail].cmd, cmd, sizeof(env->queue[env->queue_tail].cmd) - 1);
        env->queue[env->queue_tail].cmd[sizeof(env->queue[env->queue_tail].cmd) - 1] = '\0';
        strncpy(env->queue[env->queue_tail].nick, nick, sizeof(env->queue[env->queue_tail].nick) - 1);
        env->queue[env->queue_tail].nick[sizeof(env->queue[env->queue_tail].nick) - 1] = '\0';
        env->queue_tail = (env->queue_tail + 1) % QUEUE_SIZE;
        pthread_cond_signal(&env->queue_cond);
    } else {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Error: Command queue full for %s, command '%s' ignored", nick, cmd);
        send_to_channel(err_msg);
    }
    pthread_mutex_unlock(&env->queue_mutex);
}

// Retire et retourne une commande de la file d’attente d’un environnement
Command *dequeue(Env *env) {
    if (!env) return NULL;

    pthread_mutex_lock(&env->queue_mutex);
    if (env->queue_head == env->queue_tail) {
        // File vide
        pthread_mutex_unlock(&env->queue_mutex);
        return NULL;
    }

    // Allouer une structure Command pour retourner la commande
    Command *cmd = malloc(sizeof(Command));
    if (!cmd) {
        pthread_mutex_unlock(&env->queue_mutex);
        return NULL;
    }

    // Copier la commande depuis la file
    *cmd = env->queue[env->queue_head];
    env->queue_head = (env->queue_head + 1) % QUEUE_SIZE;

    pthread_mutex_unlock(&env->queue_mutex);
    return cmd;
}

int main(int argc, char *argv[]) {
    char *server_name = "labynet.fr";
    char bot_nick[512] = "forth";
    channel = "#labynet";

    // Gestion des arguments en ligne de commande
    if (argc == 4) {
        server_name = argv[1];
        strncpy(bot_nick, argv[2], sizeof(bot_nick) - 1);
        bot_nick[sizeof(bot_nick) - 1] = '\0';
        channel = argv[3];
    } else if (argc != 1) {
        printf("Usage: %s [server nick channel]\n", argv[0]);
        return 1;
    }

    printf("Starting Forth IRC bot on %s with nick %s in channel %s\n", server_name, bot_nick, channel);

    // Boucle principale avec reconnexion automatique
    while (1) {
        // Tentative de connexion si déconnecté
        if (irc_socket == -1) {
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            int status = getaddrinfo(server_name, "6667", &hints, &res);
            if (status != 0) {
                printf("getaddrinfo failed: %s, retrying in 5s...\n", gai_strerror(status));
                sleep(5);
                continue;
            }

            char server_ip[INET_ADDRSTRLEN];
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
            inet_ntop(AF_INET, &(ipv4->sin_addr), server_ip, sizeof(server_ip));
            printf("Connecting to %s with nick %s...\n", server_ip, bot_nick);
            irc_connect(server_ip, bot_nick);
            freeaddrinfo(res);

            if (irc_socket == -1) {
                printf("Connection failed, retrying in 5s...\n");
                sleep(5);
                continue;
            }
            printf("Connected to %s\n", server_ip);
        }

        char buffer[4096];
        size_t buffer_pos = 0;
        int registered = 0;

        // Boucle de réception des messages IRC
        while (irc_socket != -1) {
            if (irc_receive(buffer, sizeof(buffer), &buffer_pos) == -1) {
                printf("Disconnected from IRC, closing socket...\n");
                close(irc_socket);
                irc_socket = -1;
                sleep(1); // Petite pause avant reconnexion
                break;
            }

            char *start = buffer;
            char *end;
            while ((end = strstr(start, "\r\n"))) {
                *end = '\0';
                char *line = start;

                char nick[MAX_STRING_SIZE];
                char cmd[512];
                if (irc_handle_message(line, bot_nick, &registered, nick, cmd, sizeof(cmd))) {
                    // PRIVMSG Forth détecté
                    if (strcmp(cmd, "QUIT") == 0) {
                        Env *env = findEnv(nick);
                        if (env) {
                            if (env->in_use) {
                                char quit_msg[512];
                                snprintf(quit_msg, sizeof(quit_msg), "Operation impossible %s, essayez plus tard (opération en cours)", nick);
                                send_to_channel(quit_msg);
                            } else {
                                freeEnv(nick);
                                char quit_msg[512];
                                snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", nick);
                                send_to_channel(quit_msg);
                            }
                        } else {
                            send_to_channel("No environment found for you to quit.");
                        }
                    } else if (strcmp(cmd, "EXIT") == 0) {
                        send_to_channel("Bot shutting down...");
                        while (head) {
                            Env *temp = head;
                            head = head->next;
                            char quit_msg[512];
                            snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", temp->nick);
                            send_to_channel(quit_msg);
                            freeEnv(temp->nick);
                        }
                        if (irc_socket != -1) {
                            close(irc_socket);
                            irc_socket = -1;
                        }
                        printf("Bot terminated cleanly.\n");
                        return 0; // Sortie propre
                    } else {
                        enqueue(cmd, nick);
                    }
                }

                start = end + 2;
            }

            // Gestion des données restantes dans le buffer
            if (start != buffer) {
                size_t remaining = buffer_pos - (start - buffer);
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

    // Nettoyage final (jamais atteint à cause de la boucle infinie, mais bon à avoir)
    while (head) freeEnv(head->nick);
    if (irc_socket != -1) close(irc_socket);
    return 0;
}
