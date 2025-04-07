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

Command queue[QUEUE_SIZE];
int queue_head = 0, queue_tail = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

Env *head = NULL;
Env *currentenv = NULL;
int irc_socket = -1;
mpz_t mpz_pool[MPZ_POOL_SIZE];
char *channel;

void init_mpz_pool() {
    for (int i = 0; i < MPZ_POOL_SIZE; i++) mpz_init(mpz_pool[i]);
}

void clear_mpz_pool() {
    for (int i = 0; i < MPZ_POOL_SIZE; i++) mpz_clear(mpz_pool[i]);
}



void enqueue(const char *cmd, const char *nick) {
    pthread_mutex_lock(&queue_mutex);
    if ((queue_tail + 1) % QUEUE_SIZE != queue_head) {
        strncpy(queue[queue_tail].cmd, cmd, sizeof(queue[queue_tail].cmd) - 1);
        queue[queue_tail].cmd[sizeof(queue[queue_tail].cmd) - 1] = '\0';
        strncpy(queue[queue_tail].nick, nick, sizeof(queue[queue_tail].nick) - 1);
        queue[queue_tail].nick[sizeof(queue[queue_tail].nick) - 1] = '\0';
        queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    }
    pthread_mutex_unlock(&queue_mutex);
}

Command *dequeue() {
    Command *cmd = NULL;
    pthread_mutex_lock(&queue_mutex);
    if (queue_head != queue_tail) {
        cmd = &queue[queue_head];
        queue_head = (queue_head + 1) % QUEUE_SIZE;
    }
    pthread_mutex_unlock(&queue_mutex);
    return cmd;
}

struct irc_message {
    char *prefix;
    char *command;
    char **args;
    int arg_count;
};
 
void parse_irc_message(const char *line, struct irc_message *msg) {
    msg->prefix = NULL;
    msg->command = NULL;
    msg->args = NULL;
    msg->arg_count = 0;

    if (!line || !*line) return;

    char *copy = strdup(line);
    if (!copy) return;

    char *token;
    char *rest = copy;

    if (copy[0] == ':') {
        token = strtok_r(rest, " ", &rest);
        if (token) msg->prefix = strdup(token + 1);
    }

    token = strtok_r(rest, " ", &rest);
    if (token) msg->command = strdup(token);

    int max_args = 10;
    msg->args = malloc(max_args * sizeof(char *));
    if (!msg->args) {
        free(copy);
        return;
    }

    int arg_index = 0;
    while ((token = strtok_r(rest, " ", &rest))) {
        if (arg_index >= max_args) break;
        if (token[0] == ':') {
            size_t len = strlen(token + 1);
            char *arg = malloc(len + 1);
            if (!arg) break;
            strcpy(arg, token + 1);
            if (rest && *rest) {
                size_t rest_len = strlen(rest);
                char *temp = malloc(len + rest_len + 2);
                if (!temp) {
                    free(arg);
                    break;
                }
                strcpy(temp, arg);
                strcat(temp, " ");
                strcat(temp, rest);
                free(arg);
                msg->args[arg_index++] = temp;
                break;
            } else {
                msg->args[arg_index++] = arg;
                break;
            }
        }
        msg->args[arg_index++] = strdup(token);
    }
    msg->arg_count = arg_index;

    free(copy);
}
 
 
void free_irc_message(struct irc_message *msg) {
    if (msg->prefix) free(msg->prefix);
    if (msg->command) free(msg->command);
    if (msg->args) {
        for (int i = 0; i < msg->arg_count; i++) {
            if (msg->args[i]) free(msg->args[i]);
        }
        free(msg->args);
    }
    msg->prefix = NULL;
    msg->command = NULL;
    msg->args = NULL;
    msg->arg_count = 0;
}

int main(int argc, char *argv[]) {
    char *server_name = "labynet.fr";
    char bot_nick[512] = "forth";
    channel = "#labynet";
    struct addrinfo hints, *res;

    if (argc == 4) { // Corriger la condition
        server_name = argv[1];
        strncpy(bot_nick, argv[2], sizeof(bot_nick) - 1);
        bot_nick[sizeof(bot_nick) - 1] = '\0';
        channel = argv[3];
    }

    init_mpz_pool();

    pthread_t interpret_tid;
    if (pthread_create(&interpret_tid, NULL, interpret_thread, NULL) != 0) {
        perror("Failed to create interpret thread");
        exit(1);
    }
    pthread_detach(interpret_tid);

    while (1) {
        //printf("Resolving %s...\n", server_name);
        //fflush(stdout);
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        int status = getaddrinfo(server_name, "6667", &hints, &res);
        if (status != 0) {
            printf("getaddrinfo failed: %s\n", gai_strerror(status));
            fflush(stdout);
            sleep(5);
            continue;
        }

        char server_ip[INET_ADDRSTRLEN];
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), server_ip, sizeof(server_ip));
        printf("Trying to connect to %s with nick %s...\n", server_ip, bot_nick);
        fflush(stdout);
        irc_connect(server_ip, bot_nick);
        freeaddrinfo(res);

        if (irc_socket == -1) {
            printf("Failed to connect to %s\n", server_ip);
            fflush(stdout);
            sleep(5);
            continue;
        }
        printf("Connected to %s\n", server_ip);
        fflush(stdout);

        char buffer[4096];
        size_t buffer_pos = 0;
        int registered = 0;

        while (1) {
            //printf("Waiting for data...\n");
            //fflush(stdout);
            int bytes = recv(irc_socket, buffer + buffer_pos, sizeof(buffer) - buffer_pos - 1, 0);
            if (bytes <= 0) {
                printf("Disconnected\n");
                fflush(stdout);
                close(irc_socket);
                irc_socket = -1;
                sleep(5);
                break;
            }
            buffer_pos += bytes;
            buffer[buffer_pos] = '\0';
            // printf("Received %d bytes: %s\n", bytes, buffer);
            // fflush(stdout);

            char *start = buffer;
            char *end;
            while ((end = strstr(start, "\r\n"))) {
                *end = '\0';
                char *line = start;
                // printf("Processing line: %s\n", line);
                // fflush(stdout);

                struct irc_message msg = {0};
                parse_irc_message(line, &msg);
                /*if (msg.command) {
                    printf("Command: %s, Args: %d\n", msg.command, msg.arg_count);
                    for (int i = 0; i < msg.arg_count; i++) {
                        printf("Arg %d: %s\n", i, msg.args[i]);
                    }
                    fflush(stdout);
                }
  				*/
                if (msg.command) {
                    if (strcmp(msg.command, "001") == 0) {
                        registered = 1;
                        //printf("Registration complete, joining %s\n", channel);
                        // fflush(stdout);
                        char join_msg[512];
                        snprintf(join_msg, sizeof(join_msg), "JOIN %s\r\n", channel);
                        send(irc_socket, join_msg, strlen(join_msg), 0);
                        // printf("Sent: %s", join_msg);
                        // fflush(stdout);
                    } else if (strcmp(msg.command, "433") == 0) {
                        char new_nick[512];
                    	 snprintf(new_nick, sizeof(new_nick), "%s_", bot_nick);
                        char nick_msg[512];
                        snprintf(nick_msg, sizeof(nick_msg), "NICK %s\r\n", new_nick);
                        send(irc_socket, nick_msg, strlen(nick_msg), 0);
                        //printf("Nickname %s in use, trying %s\n", bot_nick, new_nick);
                        //fflush(stdout);
                        strncpy(bot_nick, new_nick, sizeof(bot_nick) - 1);
                        bot_nick[sizeof(bot_nick) - 1] = '\0';
                    } else if (strcmp(msg.command, "PING") == 0 && msg.arg_count > 0) {
                        char pong[512];
                        snprintf(pong, sizeof(pong), "PONG :%s\r\n", msg.args[0]);
                        send(irc_socket, pong, strlen(pong), 0);
                        //printf("Sent PONG\n");
                        //fflush(stdout);
                    } else if (registered && strcmp(msg.command, "PRIVMSG") == 0 && msg.arg_count >= 2) {
                        if (strcmp(msg.args[0], channel) == 0) {
                            char *cmd_start = msg.args[1];
                            char prefix[512];
                            snprintf(prefix, sizeof(prefix), "%s:", bot_nick);
                            if (strncmp(cmd_start, prefix, strlen(prefix)) == 0) {
                                char *trimmed_cmd = cmd_start + strlen(prefix);
                                while (isspace((unsigned char)*trimmed_cmd)) trimmed_cmd++;
                                size_t len = strlen(trimmed_cmd);
                                while (len > 0 && isspace((unsigned char)trimmed_cmd[len - 1])) {
                                    trimmed_cmd[--len] = '\0';
                                }

                                char nick[MAX_STRING_SIZE];
                                if (msg.prefix) {
                                    char *nick_end = strchr(msg.prefix, '!');
                                    if (nick_end) {
                                        strncpy(nick, msg.prefix, nick_end - msg.prefix);
                                        nick[nick_end - msg.prefix] = '\0';
                                    } else {
                                        strcpy(nick, msg.prefix);
                                    }
                                } else {
                                    strcpy(nick, "unknown");
                                }
                                //printf("Enqueuing: %s for %s\n", trimmed_cmd, nick);
                                //fflush(stdout);

                                if (strcmp(trimmed_cmd, "QUIT") == 0) {
                                    Env *env = findEnv(nick);
                                    if (env) {
                                         if (env->in_use)
                                         {
                                        
                                        char quit_msg[512];
                                        snprintf(quit_msg, sizeof(quit_msg), "Operation impossible %s essayez plus tard ", nick);
                                        send_to_channel(quit_msg);
                                        }
                                    else {
                                        freeEnv(nick);
                                        char quit_msg[512];
                                        snprintf(quit_msg, sizeof(quit_msg), "Environment for %s has been freed.", nick);
                                        send_to_channel(quit_msg);
                                    }
                                    } else {
                                        send_to_channel("No environment found for you to quit.");
                                    }
                                } else {
                                    enqueue(trimmed_cmd, nick);
                                }
                            }
                        }
                    }
                }

                free_irc_message(&msg);

            
                start = end + 2;
            }

            if (start != buffer) {
                size_t remaining = buffer_pos - (start - buffer);
                memmove(buffer, start, remaining);
                buffer_pos = remaining;
                buffer[buffer_pos] = '\0';
            } else if (buffer_pos == sizeof(buffer) - 1) {
                printf("Buffer full, resetting\n");
                fflush(stdout);
                buffer_pos = 0;
                buffer[0] = '\0';
            }
        }
    }

    while (head) freeEnv(head->nick);
    clear_mpz_pool();
    if (irc_socket != -1) close(irc_socket);
    return 0;
}
