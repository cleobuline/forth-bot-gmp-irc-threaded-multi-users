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

void send_to_channel(const char *msg) {
    if (irc_socket != -1) {
        char response[512];
        size_t msg_len = strlen(msg);
        size_t chunk_size = 400;
        size_t offset = 0;

        while (offset < msg_len) {
            size_t remaining = msg_len - offset;
            size_t current_chunk_size = (remaining > chunk_size) ? chunk_size : remaining;
            if (current_chunk_size < remaining && strchr(msg + offset, ' ') != NULL) {
                for (size_t i = current_chunk_size; i > 0; i--) {
                    if (msg[offset + i] == ' ') {
                        current_chunk_size = i;
                        break;
                    }
                }
            }
            char chunk[401];
            strncpy(chunk, msg + offset, current_chunk_size);
            chunk[current_chunk_size] = '\0';
            snprintf(response, sizeof(response), "PRIVMSG %s :%s\r\n", channel, chunk);
            if (send(irc_socket, response, strlen(response), 0) < 0) {
                printf("Failed to send to channel: %s\n", chunk);
                break;
            }
            offset += current_chunk_size;
            while (offset < msg_len && msg[offset] == ' ') offset++;
        }
    } else {
        printf("IRC socket not initialized\n");
    }
}
void irc_connect(const char *server_ip, const char *bot_nick, const char *channel) {
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

    snprintf(buffer, sizeof(buffer), "JOIN %s\r\n", channel);
    send(irc_socket, buffer, strlen(buffer), 0);
}

 
 
