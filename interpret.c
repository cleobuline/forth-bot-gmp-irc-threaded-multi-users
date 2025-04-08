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
        // Ignorer les commentaires Forth (entre parenthèses)
        if (strcmp(token, "(") == 0) {
            char *end = strstr(saveptr, ")");
            if (end) {
                saveptr = end + 1;
            } else {
                saveptr = NULL;  // Fin du commentaire non trouvée, on arrête
            }
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }

        // Compiler ou interpréter le token
        compileToken(token, &saveptr, currentenv);

        // Si saveptr est NULL, on a fini de parser
        if (!saveptr) break;

        // Passer au token suivant
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    // Si on est en mode compilation et qu’il reste des structures de contrôle non terminées
    if (currentenv->compiling && currentenv->control_stack_top > 0) {
        set_error("Incomplete definition: unmatched control structures");
        currentenv->compile_error = 1;
        currentenv->compiling = 0;
        currentenv->control_stack_top = 0;

        // Libérer les ressources de currentWord pour éviter les fuites
        if (currentenv->currentWord.name) {
            free(currentenv->currentWord.name);
            currentenv->currentWord.name = NULL;
        }
        for (int j = 0; j < currentenv->currentWord.string_count; j++) {
            if (currentenv->currentWord.strings[j]) {
                free(currentenv->currentWord.strings[j]);
                currentenv->currentWord.strings[j] = NULL;
            }
        }
        if (currentenv->currentWord.code) {
            free(currentenv->currentWord.code);
            currentenv->currentWord.code = NULL;
        }
        if (currentenv->currentWord.strings) {
            free(currentenv->currentWord.strings);
            currentenv->currentWord.strings = NULL;
        }
        currentenv->currentWord.code_length = 0;
        currentenv->currentWord.code_capacity = 0;
        currentenv->currentWord.string_count = 0;
        currentenv->currentWord.string_capacity = 0;
    }
}
