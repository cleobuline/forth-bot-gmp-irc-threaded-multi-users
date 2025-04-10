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
 /* OLD VERSION 
void interpret(char *input, Stack *stack, Env *env) {
    if (!env) {
        send_to_channel("DEBUG: env is NULL");
        return;
    }
    env->error_flag = 0;
    env->compile_error = 0;
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
        if (!saveptr) break;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

   
    // Si on est en mode compilation et qu’il reste des structures de contrôle non terminées
    if (env->compiling && env->control_stack_top > 0) {
        set_error(env,"Incomplete definition: unmatched control structures");
        env->compile_error = 1;
        env->compiling = 0;
        env->control_stack_top = 0;

        // Libérer les ressources de currentWord pour éviter les fuites
        if (env->currentWord.name) {
            free(env->currentWord.name);
            env->currentWord.name = NULL;
        }
        for (int j = 0; j < env->currentWord.string_count; j++) {
            if (env->currentWord.strings[j]) {
                free(env->currentWord.strings[j]);
                env->currentWord.strings[j] = NULL;
            }
        }
        if (env->currentWord.code) {
            free(env->currentWord.code);
            env->currentWord.code = NULL;
        }
        if (env->currentWord.strings) {
            free(env->currentWord.strings);
            env->currentWord.strings = NULL;
        }
        env->currentWord.code_length = 0;
        env->currentWord.code_capacity = 0;
        env->currentWord.string_count = 0;
        env->currentWord.string_capacity = 0;
    }
}
    */ 
    
// new version to check for aborted defintion 
 /*
void interpret(char *input, Stack *stack, Env *env) {
    if (!env) {
        send_to_channel("DEBUG: env is NULL");
        return;
    }

    env->error_flag = 0;
    env->compile_error = 0;

    // Ne pas réinitialiser env->compiling ici, on vérifie juste les restes
    if (env->compiling) {
        set_error(env, "Previous compilation state detected, checking...");
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

        // Si erreur en compilation, sortir sans réinitialiser encore
        if (env->compile_error && env->compiling) {
            // set_error(env, "Compilation error detected, will abort at end");
            break;
        }

        if (!saveptr) break;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    // Gestion finale
    if (env->compiling && !env->compile_error && env->control_stack_top == 0) {
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
    } else if (env->compiling) {
        set_error(env, "Definition discarded due to error or incomplete structure");
        freeCurrentWord(env);
        env->compiling = 0;
        env->control_stack_top = 0;
        env->current_word_index = -1;
        env->compile_error = 0;
        env->dictionary.count--;// décremente pour cette definition avortée 
    }
}
 */
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
    }
    // Sinon, on laisse env->compiling intact pour le prochain message
}
