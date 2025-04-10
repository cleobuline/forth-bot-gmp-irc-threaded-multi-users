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
 

void *env_interpret_thread(void *arg) {
    Env *env = (Env *)arg;
    while (env->thread_running) {
        Command *cmd = dequeue(env);  // Récupérer la prochaine commande
        if (cmd) {
            pthread_mutex_lock(&env->in_use_mutex);
            env->in_use = 1;
            pthread_mutex_unlock(&env->in_use_mutex);

            interpret(cmd->cmd, &env->main_stack, env);

            pthread_mutex_lock(&env->in_use_mutex);
            env->in_use = 0;
            pthread_mutex_unlock(&env->in_use_mutex);

            // Libérer la commande allouée par dequeue
            free(cmd);
        } else {
            // Pas de commande, attendre un signal
            pthread_mutex_lock(&env->queue_mutex);
            if (env->thread_running && env->queue_head == env->queue_tail) {
                pthread_cond_wait(&env->queue_cond, &env->queue_mutex);
            }
            pthread_mutex_unlock(&env->queue_mutex);
        }
    }
    return NULL;
}

 void interpret(char *input, Stack *stack, Env *env) {
    if (!env) {
        send_to_channel("DEBUG: env is NULL");
        return;
    }

    env->error_flag = 0;
    env->compile_error = 0;

    // Si en cours de compilation, on continue sans réinitialiser
    if (env->compiling) {
        // Optionnel : informer l'utilisateur que la définition continue
        // send_to_channel("Continuing previous compilation...");
    }

    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);
    while (token && !env->error_flag && !env->compile_error) {
        if (strcmp(token, "(") == 0) {
            char *end = strstr(saveptr, ")");
            if (end) saveptr = end + 1;
            else saveptr = NULL;
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }

        compileToken(token, &saveptr, env);

        if (env->compile_error && env->compiling) {
            break;
        }

        if (!saveptr) break;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    // Gestion finale : uniquement si la définition est terminée ou explicitement erronée
    if (env->compiling && !env->compile_error && env->control_stack_top == 0) {
        if (strchr(input, ';')) { // Vérifie si ';' est dans ce message
            if (env->current_word_index >= 0 && env->current_word_index < env->dictionary.capacity) {
                CompiledWord *dict_word = &env->dictionary.words[env->current_word_index];
                if (dict_word->name) free(dict_word->name);
                if (dict_word->code) free(dict_word->code);
                for (int j = 0; j < dict_word->string_count; j++) {
                    if (dict_word->strings[j]) free(dict_word->strings[j]);
                }
                if (dict_word->strings) free(dict_word->strings);

                *dict_word = env->currentWord;
                env->currentWord.name = NULL;
                env->currentWord.code = NULL;
                env->currentWord.strings = NULL;
            }
            env->compiling = 0;
            env->control_stack_top = 0;
            env->current_word_index = -1;
        }
    } else if (env->compiling && env->compile_error) {
        set_error(env, "Definition discarded due to error");
        freeCurrentWord(env);
        env->compiling = 0;
        env->control_stack_top = 0;
        env->current_word_index = -1;
        env->compile_error = 0;
        env->dictionary.count--;
        env->main_stack.top = -1; // Réinitialise la pile principale
    	env->return_stack.top = -1; // Réinitialise la pile de retour
    }
    // Sinon, on laisse env->compiling intact pour le prochain message
}
