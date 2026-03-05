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
#include <errno.h>
#include "memory_forth.h"
#include "forth_bot.h"

void *env_interpret_thread(void *arg) {
    Env *env = (Env *)arg;

    while (env->thread_running) {
        Command *cmd = dequeue(env);
        if (cmd) {
            interpret(cmd->cmd, &env->main_stack, env);
            free(cmd);
        } else {
            pthread_mutex_lock(&env->queue_mutex);
            if (env->thread_running && env->queue_head == env->queue_tail) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 7200;
                int rc = pthread_cond_timedwait(&env->queue_cond, &env->queue_mutex, &ts);
                if (rc == ETIMEDOUT && env->queue_head == env->queue_tail && env->thread_running) {
                    char msg[512];
                    char nick_copy[MAX_STRING_SIZE];
                    snprintf(msg, sizeof(msg), "Environment for %s inactive, freeing...", env->nick);
                    strncpy(nick_copy, env->nick, sizeof(nick_copy) - 1);
                    nick_copy[sizeof(nick_copy) - 1] = '\0';
                    pthread_mutex_unlock(&env->queue_mutex);

                    pthread_detach(pthread_self());
                    send_to_channel(msg);

                    pthread_rwlock_wrlock(&env_rwlock);
                    env->thread = 0;
                    pthread_rwlock_unlock(&env_rwlock);

                    freeEnv(nick_copy);
                    return NULL;
                }
            }
            pthread_mutex_unlock(&env->queue_mutex);
        }
    }

    return NULL;
}

void interpret(char *input, Stack *stack __attribute__((unused)), Env *env) {
    if (!env) return;

    env->error_flag    = 0;
    env->compile_error = 0;

    char *saveptr;
    char *token = strtok_r(input, " \t\n", &saveptr);

    while (token && !env->error_flag && !env->cancel_flag) {

        // Commentaires entre parenthèses : ( ... )
        if (strcmp(token, "(") == 0) {
            char *end = saveptr ? strstr(saveptr, ")") : NULL;
            if (end) saveptr = end + 1;
            else     saveptr = NULL;
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }

        compileToken(token, &saveptr, env);

        // Si erreur de compilation, compileToken a déjà tout nettoyé
        // (compiling=0, freeCurrentWord, dictionary.count--).
        // On arrête la boucle mais il n'y a rien d'autre à faire ici.
        if (env->compile_error) break;

        if (!saveptr) break;
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    // La finalisation d'une définition (";") est entièrement gérée dans
    // compileToken au moment où il rencontre le token ";".
    // interpret() n'a plus à s'en occuper.
    //
    // Si on sort de la boucle avec compiling=1 et sans erreur, c'est normal :
    // la définition est multi-commandes IRC (l'utilisateur a envoyé ": FOO 42"
    // puis ". ;" en deux messages séparés). On attend simplement la suite.
}
