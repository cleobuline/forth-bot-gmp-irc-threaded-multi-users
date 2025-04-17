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

void handle_sigusr1(int sig) {
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

int main(int argc, char *argv[]) {
    char *server_name = "labynet.fr";
    char bot_nick[512] = "forth";
    char default_channel[] = "#labynet";

    init_globals();
    main_thread = pthread_self();

    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

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

    printf("Starting Forth IRC bot on %s with nick %s in channel %s\n", server_name, bot_nick, channel);

    int reconnect_delay = 5;
    while (!shutdown_flag) {
        if (irc_socket == -1) {
            struct addrinfo hints, *res;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            int status = getaddrinfo(server_name, "6667", &hints, &res);
            if (status != 0) {
                printf("getaddrinfo failed: %s, retrying in %ds...\n", gai_strerror(status), reconnect_delay);
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

            if (pthread_create(&irc_sender_thread_id, NULL, irc_sender_thread, NULL) != 0) {
                printf("Failed to create IRC sender thread\n");
                close(irc_socket);
                irc_socket = -1;
                return 1;
            }
        }

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
            while ((end = strstr(start, "\r\n")) && !shutdown_flag) {
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
                                char quit_msg[512];
                                snprintf(quit_msg, sizeof(quit_msg), "Operation impossible %s, try later (commands pending)", nick);
                                send_to_channel(quit_msg);
                                pthread_mutex_unlock(&env->queue_mutex);
                            } else {
                                pthread_mutex_unlock(&env->queue_mutex);
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
                        shutdown_flag = 1;
                        pthread_cond_broadcast(&irc_msg_queue_cond);
                        break;
                    } else {
                        enqueue(cmd, nick);
                    }
                }

                start = end + 2;
            }

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

    if (irc_socket != -1) {
        send_to_channel("Bot shutting down due to signal...");
        close(irc_socket);
        irc_socket = -1;
    }

    while (head) {
        Env *temp = head;
        head = head->next;
        char quit_msg[512];
        snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", temp->nick);
        send_to_channel(quit_msg);
        freeEnv(temp->nick);
    }

    if (irc_sender_thread_id) {
        pthread_join(irc_sender_thread_id, NULL);
    }
    if (channel) {
        free(channel);
    }
    printf("Bot terminated cleanly.\n");
    return 0;
}
