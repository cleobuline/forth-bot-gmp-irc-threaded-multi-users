
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
#include "memory_forth.h"
#include "forth_bot.h"

// Implémentation personnalisée de memrchr
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

    if (!line || !*line) return;

    char *copy = strdup(line);
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

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6667);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    if (connect(irc_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(irc_socket);
        irc_socket = -1;
        return;
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "NICK %s\r\n", bot_nick);
    send(irc_socket, buffer, strlen(buffer), 0);

    snprintf(buffer, sizeof(buffer), "USER %s 0 * :%s\r\n", bot_nick, bot_nick);
    send(irc_socket, buffer, strlen(buffer), 0);
}
 
// Envoie un chunk de message au canal IRC, avec vérification des tailles
static int send_chunk(int socket, const char *channel, const char *msg, size_t len) {
    char buffer[512];
    size_t prefix_len = snprintf(buffer, sizeof(buffer), "PRIVMSG %s :", channel);
    
    // Vérifier que le préfixe et le message tiennent dans le buffer
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

// Envoie un message au canal IRC avec segmentation sécurisée
void send_to_channel(const char *msg) {
    if (irc_socket == -1) {
        fprintf(stderr, "send_to_channel: IRC socket not initialized\n");
        return;
    }
    if (!msg || !*msg) {
        return; // Rien à envoyer
    }

    pthread_mutex_lock(&irc_mutex);
    const size_t max_chunk = 400; // Limite IRC typique
    size_t msg_len = strlen(msg);
    size_t offset = 0;

    while (offset < msg_len) {
        size_t remaining = msg_len - offset;
        size_t chunk_size = (remaining > max_chunk) ? max_chunk : remaining;

        // Ajuster chunk_size pour couper à un espace si possible
        if (chunk_size == max_chunk) {
            const char *space = memrchr(msg + offset, ' ', chunk_size);
            if (space) {
                chunk_size = space - (msg + offset);
            }
        }

        // Envoyer le chunk
        if (send_chunk(irc_socket, channel, msg + offset, chunk_size) < 0) {
            // Ne pas tenter de reconnecter ici pour éviter les problèmes de concurrence
            pthread_mutex_unlock(&irc_mutex);
            return;
        }

        offset += chunk_size;
        // Sauter les espaces en début de prochain chunk
        while (offset < msg_len && msg[offset] == ' ') {
            offset++;
        }
    }

    pthread_mutex_unlock(&irc_mutex);
}
 
// Nouvelle fonction : Recevoir les données IRC
int irc_receive(char *buffer, size_t buffer_size, size_t *buffer_pos) {
    if (irc_socket == -1) return -1;
    int bytes = recv(irc_socket, buffer + *buffer_pos, buffer_size - *buffer_pos - 1, 0);
    if (bytes <= 0) {
        if (bytes == -1 && errno == EINTR) {
            printf("Receive interrupted by signal, continuing...\n");
            return 0; // Ne pas considérer comme une déconnexion
        }
        printf("Disconnected: %s\n", bytes == 0 ? "closed by server" : strerror(errno));
        return -1;
    }
    *buffer_pos += bytes;
    buffer[*buffer_pos] = '\0';
    return 0;
}
// Nouvelle fonction : Gérer les messages IRC standards
int irc_handle_message(const char *line, char *bot_nick, int *registered, char *nick_out, char *cmd_out, size_t cmd_out_size) {
    struct irc_message msg = {0};
    parse_irc_message(line, &msg);

    if (!msg.command) {
        free_irc_message(&msg);
        return 0; // Pas de commande, rien à faire
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
                    return 1; // Indique qu’un PRIVMSG Forth a été trouvé
                }
            }
        }
    }

    free_irc_message(&msg);
    return 0; // Pas un PRIVMSG Forth
}
