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
        Command cmd_buffer = {0};  // Buffer local pour la commande
        int has_cmd = 0;

        pthread_mutex_lock(&env->queue_mutex);
        while (env->queue_head == env->queue_tail && env->thread_running) {
            pthread_cond_wait(&env->queue_cond, &env->queue_mutex);  // Attend un signal
        }
        if (env->queue_head != env->queue_tail) {
            cmd_buffer = env->queue[env->queue_head];  // Copie locale pour éviter des problèmes
            env->queue_head = (env->queue_head + 1) % QUEUE_SIZE;
            has_cmd = 1;
        }
        pthread_mutex_unlock(&env->queue_mutex);

        if (has_cmd) {
            pthread_mutex_lock(&env->in_use_mutex);
            env->in_use = 1;
            pthread_mutex_unlock(&env->in_use_mutex);

            interpret(cmd_buffer.cmd, &env->main_stack, env);

            pthread_mutex_lock(&env->in_use_mutex);
            env->in_use = 0;
            pthread_mutex_unlock(&env->in_use_mutex);
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
