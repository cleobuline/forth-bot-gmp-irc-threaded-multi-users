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

int main(int argc, char *argv[]) {
    char *server_name = "labynet.fr";
    char bot_nick[512] = "forth";
    channel = "#labynet";

    if (argc == 4) {
        server_name = argv[1];
        strncpy(bot_nick, argv[2], sizeof(bot_nick) - 1);
        bot_nick[sizeof(bot_nick) - 1] = '\0';
        channel = argv[3];
    }

    while (1) {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        int status = getaddrinfo(server_name, "6667", &hints, &res);
        if (status != 0) {
            printf("getaddrinfo failed: %s\n", gai_strerror(status));
            sleep(5);
            continue;
        }

        char server_ip[INET_ADDRSTRLEN];
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), server_ip, sizeof(server_ip));
        printf("Trying to connect to %s with nick %s...\n", server_ip, bot_nick);
        irc_connect(server_ip, bot_nick);
        freeaddrinfo(res);

        if (irc_socket == -1) {
            printf("Failed to connect to %s\n", server_ip);
            sleep(5);
            continue;
        }
        printf("Connected to %s\n", server_ip);

        char buffer[4096];
        size_t buffer_pos = 0;
        int registered = 0;

        while (1) {
            if (irc_receive(buffer, sizeof(buffer), &buffer_pos) == -1) {
                close(irc_socket);
                irc_socket = -1;
                sleep(5);
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
                                snprintf(quit_msg, sizeof(quit_msg), "Operation impossible %s essayez plus tard ( opération en cours ) ", nick);
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
                            char* nick = temp->nick;
                                                            char quit_msg[512];
                                snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", nick);
                                send_to_channel(quit_msg);
                            freeEnv(temp->nick);
                        }
                        if (irc_socket != -1) {
                            close(irc_socket);
                            irc_socket = -1;
                        }
                        exit(0);
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
                printf("Buffer full, resetting\n");
                send_to_channel("Warning: IRC buffer full, data may be lost");
                buffer_pos = 0;
                buffer[0] = '\0';
            }
        }
    }

    while (head) freeEnv(head->nick);
    if (irc_socket != -1) close(irc_socket);
    return 0;
}
