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

void *interpret_thread(void *arg) {
 
    while (1) {
        Command *cmd = dequeue();
        if (cmd) {
 
            Env *env = findEnv(cmd->nick);
            if (!env) {
                env = createEnv(cmd->nick);
                if (!env) {
                    printf("Failed to create env for %s\n", cmd->nick);
                    fflush(stdout);
                    continue;
                }
                set_currentenv(env);

                initDictionary(env);
            } else {
               set_currentenv(env);
            }

            if (!currentenv) {
                printf("currentenv is NULL for %s!\n", cmd->nick);
                fflush(stdout);
                send_to_channel("Error: Environment not initialized");
            } else {
                env->in_use= 1;
                interpret(cmd->cmd, &env->main_stack);
                env->in_use= 0;
            }
        } else {
            usleep(10000); // 10ms pour éviter une boucle trop rapide
        }
    }
    return NULL;
}

 
 
 void interpret(char *input, Stack *stack) {
    if (!currentenv) {
        send_to_channel("DEBUG: currentenv is NULL");
        return;
    }
 
    currentenv->error_flag = 0;
    currentenv->compile_error = 0;
    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);
    while (token && !currentenv->error_flag && !currentenv->compile_error) {
 
        if (strcmp(token, "(") == 0) {
            char *end = strstr(saveptr, ")");
            if (end) saveptr = end + 1;
            else saveptr = NULL;
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }
        compileToken(token, &saveptr, currentenv);
        if (!saveptr) break;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
}
