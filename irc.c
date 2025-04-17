#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <gmp.h>
#include <ctype.h>
#include <curl/curl.h>
#include <fcntl.h>
#include "memory_forth.h"
#include "forth_bot.h"

pthread_mutex_t irc_msg_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t irc_msg_queue_cond = PTHREAD_COND_INITIALIZER;
IrcMessage irc_msg_queue[IRC_MSG_QUEUE_SIZE];
int irc_msg_queue_head = 0;
int irc_msg_queue_tail = 0;

void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (p-- > (const unsigned char *)s) {
        if (*p == (unsigned char)c) {
            return (void *)p;
        }
    }
    return NULL;
}

void parse_irc_message(const char *line, struct irc_message *msg) {
    msg->prefix = NULL;
    msg->command = NULL;
    msg->args = NULL;
    msg->arg_count = 0;

    if (!line || !*line || strlen(line) > 2048) return;

    char *copy = strndup(line, 2048);
    if (!copy) return;

    char *token;
    char *rest = copy;

    if (copy[0] == ':') {
        token = strtok_r(rest, " ", &rest);
        if (token) msg->prefix = strdup(token + 1);
        if (!msg->prefix) {
            free(copy);
            return;
        }
    }

    token = strtok_r(rest, " ", &rest);
    if (token) msg->command = strdup(token);
    if (!msg->command) {
        free(copy);
        free(msg->prefix);
        return;
    }

    int max_args = 10;
    msg->args = malloc(max_args * sizeof(char *));
    if (!msg->args) {
        free(copy);
        free(msg->prefix);
        free(msg->command);
        return;
    }

    int arg_index = 0;
    while ((token = strtok_r(rest, " ", &rest))) {
        if (arg_index >= max_args || strlen(token) > 512) continue;
        if (arg_index >= max_args) {
            max_args *= 2;
            char **new_args = SAFE_REALLOC(msg->args, max_args * sizeof(char *));
            if (!new_args) break;
            msg->args = new_args;
        }
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
        if (!msg->args[arg_index - 1]) break;
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

void irc_connect(const char *server_ip, const char *bot_nick) {
    if (irc_socket != -1) {
        close(irc_socket);
        irc_socket = -1;
    }

    irc_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (irc_socket == -1) {
        perror("socket");
        return;
    }

    fcntl(irc_socket, F_SETFL, O_NONBLOCK);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6667);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(irc_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connect");
            close(irc_socket);
            irc_socket = -1;
            return;
        }
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "NICK %s\r\n", bot_nick);
    send(irc_socket, buffer, strlen(buffer), 0);
    snprintf(buffer, sizeof(buffer), "USER %s 0 * :%s\r\n", bot_nick, bot_nick);
    send(irc_socket, buffer, strlen(buffer), 0);
}

static int send_chunk(int socket, const char *channel, const char *msg, size_t len) {
    char buffer[512];
    size_t prefix_len = snprintf(buffer, sizeof(buffer), "PRIVMSG %s :", channel);
    if (prefix_len + len + 2 > 510) {
        len = 510 - prefix_len;
        while (len > 0 && (msg[len] & 0xC0) == 0x80) len--;
    }
    if (prefix_len >= sizeof(buffer) - 3 || prefix_len + len + 2 >= sizeof(buffer)) {
        fprintf(stderr, "send_chunk: Message too long for buffer (prefix=%zu, len=%zu)\n", prefix_len, len);
        return -1;
    }

    memcpy(buffer + prefix_len, msg, len);
    buffer[prefix_len + len] = '\r';
    buffer[prefix_len + len + 1] = '\n';
    buffer[prefix_len + len + 2] = '\0';

    ssize_t sent = send(socket, buffer, prefix_len + len + 2, 0);
    if (sent < 0) {
        fprintf(stderr, "send_chunk: Failed to send: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void send_to_channel(const char *msg) {
    if (irc_socket == -1) {
        fprintf(stderr, "send_to_channel: IRC socket not initialized\n");
        return;
    }
    if (!msg || !*msg) return;

    if (strlen(msg) >= sizeof(irc_msg_queue[0].msg)) {
        fprintf(stderr, "send_to_channel: Message too long (%zu bytes), truncating\n", strlen(msg));
    }
    enqueue_irc_msg(msg);
}

void enqueue_irc_msg(const char *msg) {
    pthread_mutex_lock(&irc_msg_queue_mutex);
    if ((irc_msg_queue_tail + 1) % IRC_MSG_QUEUE_SIZE != irc_msg_queue_head) {
        strncpy(irc_msg_queue[irc_msg_queue_tail].msg, msg, sizeof(irc_msg_queue[irc_msg_queue_tail].msg) - 1);
        irc_msg_queue[irc_msg_queue_tail].msg[sizeof(irc_msg_queue[irc_msg_queue_tail].msg) - 1] = '\0';
        irc_msg_queue[irc_msg_queue_tail].used = 1;
        irc_msg_queue_tail = (irc_msg_queue_tail + 1) % IRC_MSG_QUEUE_SIZE;
        pthread_cond_signal(&irc_msg_queue_cond);
    } else {
        fprintf(stderr, "enqueue_irc_msg: IRC message queue full, message ignored: %s\n", msg);
        send_to_channel("Warning: IRC message queue full, some messages may be lost");
    }
    pthread_mutex_unlock(&irc_msg_queue_mutex);
}

void *irc_sender_thread(void *arg) {
    while (!shutdown_flag) {
        pthread_mutex_lock(&irc_msg_queue_mutex);
        while (irc_msg_queue_head == irc_msg_queue_tail && !shutdown_flag) {
            pthread_cond_wait(&irc_msg_queue_cond, &irc_msg_queue_mutex);
        }
        if (shutdown_flag) {
            pthread_mutex_unlock(&irc_msg_queue_mutex);
            break;
        }
        IrcMessage msg = irc_msg_queue[irc_msg_queue_head];
        irc_msg_queue_head = (irc_msg_queue_head + 1) % IRC_MSG_QUEUE_SIZE;
        pthread_mutex_unlock(&irc_msg_queue_mutex);

        if (msg.used) {
            pthread_mutex_lock(&irc_mutex);
            if (irc_socket != -1) {
                const size_t max_chunk = 400;
                size_t msg_len = strlen(msg.msg);
                size_t offset = 0;

                // fprintf(stderr, "irc_sender_thread: Sending message: %s\n", msg.msg);

                while (offset < msg_len) {
                    size_t remaining = msg_len - offset;
                    size_t chunk_size = (remaining > max_chunk) ? max_chunk : remaining;

                    if (chunk_size == max_chunk) {
                        const char *space = memrchr(msg.msg + offset, ' ', chunk_size);
                        if (space) {
                            chunk_size = space - (msg.msg + offset);
                        }
                    }

                    if (send_chunk(irc_socket, channel, msg.msg + offset, chunk_size) < 0) {
                        fprintf(stderr, "irc_sender_thread: Failed to send chunk at offset %zu\n", offset);
                        pthread_mutex_unlock(&irc_mutex);
                        break;
                    }

                    offset += chunk_size;
                    while (offset < msg_len && msg.msg[offset] == ' ') {
                        offset++;
                    }
                }
            } else {
                fprintf(stderr, "irc_sender_thread: Socket not initialized, skipping message\n");
            }
            pthread_mutex_unlock(&irc_mutex);
        }
    }
    return NULL;
}

int irc_receive(char *buffer, size_t buffer_size, size_t *buffer_pos) {
    if (irc_socket == -1) return -1;
    int bytes = recv(irc_socket, buffer + *buffer_pos, buffer_size - *buffer_pos - 1, 0);
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) {
            printf("Receive interrupted by signal, continuing...\n");
            return 0;
        }
        printf("Disconnected: %s\n", strerror(errno));
        return -1;
    }
    if (bytes == 0) {
        printf("Disconnected: closed by server\n");
        return -1;
    }
    *buffer_pos += bytes;
    buffer[*buffer_pos] = '\0';
    return 0;
}

int irc_handle_message(const char *line, char *bot_nick, int *registered, char *nick_out, char *cmd_out, size_t cmd_out_size) {
    struct irc_message msg = {0};
    parse_irc_message(line, &msg);

    if (!msg.command) {
        free_irc_message(&msg);
        return 0;
    }

    if (strcmp(msg.command, "001") == 0) {
        *registered = 1;
        char join_msg[512];
        snprintf(join_msg, sizeof(join_msg), "JOIN %s\r\n", channel);
        pthread_mutex_lock(&irc_mutex);
        send(irc_socket, join_msg, strlen(join_msg), 0);
        pthread_mutex_unlock(&irc_mutex);
    } else if (strcmp(msg.command, "433") == 0) {
        char new_nick[512];
        snprintf(new_nick, sizeof(new_nick), "%s_", bot_nick);
        char nick_msg[512];
        snprintf(nick_msg, sizeof(nick_msg), "NICK %s\r\n", new_nick);
        pthread_mutex_lock(&irc_mutex);
        send(irc_socket, nick_msg, strlen(nick_msg), 0);
        pthread_mutex_unlock(&irc_mutex);
        strncpy(bot_nick, new_nick, 512 - 1);
        bot_nick[511] = '\0';
    } else if (strcmp(msg.command, "PING") == 0 && msg.arg_count > 0) {
        char pong[512];
        snprintf(pong, sizeof(pong), "PONG :%s\r\n", msg.args[0]);
        pthread_mutex_lock(&irc_mutex);
        send(irc_socket, pong, strlen(pong), 0);
        pthread_mutex_unlock(&irc_mutex);
    } else if (*registered && strcmp(msg.command, "PRIVMSG") == 0 && msg.arg_count >= 2) {
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

                if (nick_out && cmd_out) {
                    if (msg.prefix) {
                        char *nick_end = strchr(msg.prefix, '!');
                        if (nick_end) {
                            strncpy(nick_out, msg.prefix, nick_end - msg.prefix);
                            nick_out[nick_end - msg.prefix] = '\0';
                        } else {
                            strcpy(nick_out, msg.prefix);
                        }
                    } else {
                        strcpy(nick_out, "unknown");
                    }
                    strncpy(cmd_out, trimmed_cmd, cmd_out_size - 1);
                    cmd_out[cmd_out_size - 1] = '\0';
                    free_irc_message(&msg);
                    return 1;
                }
            }
        }
    }

    free_irc_message(&msg);
    return 0;
}
