#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory_forth.h"
#include "forth_bot.h"

void initWordHash(Env *env);

void enqueue(const char *cmd, const char *nick) {
    /* BUG 4 fix : findEnv puis createEnv n'etait pas atomique — un second
     * thread pouvait creer un env pour le meme nick entre les deux appels,
     * produisant deux environnements pour un meme utilisateur.
     * On re-verifie sous wrlock avant de creer. */
    Env *env = findEnv(nick);
    if (!env) {
        pthread_rwlock_wrlock(&env_rwlock);
        /* Re-verifier maintenant qu'on tient le verrou exclusif */
        Env *curr = head;
        while (curr && strcmp(curr->nick, nick) != 0)
            curr = curr->next;
        if (curr && !curr->being_freed) {
            env = curr;
            pthread_rwlock_unlock(&env_rwlock);
        } else {
            pthread_rwlock_unlock(&env_rwlock);
            env = createEnv(nick);
            if (!env) return;
        }
    }

    if (env->being_freed) {
        send_to_channel("enqueue: Environment is being freed, command ignored");
        return;
    }

    pthread_mutex_lock(&env->queue_mutex);
    if ((env->queue_tail + 1) % QUEUE_SIZE != env->queue_head) {
        strncpy(env->queue[env->queue_tail].cmd, cmd,
                sizeof(env->queue[env->queue_tail].cmd) - 1);
        env->queue[env->queue_tail].cmd[sizeof(env->queue[env->queue_tail].cmd) - 1] = '\0';
        strncpy(env->queue[env->queue_tail].nick, nick,
                sizeof(env->queue[env->queue_tail].nick) - 1);
        env->queue[env->queue_tail].nick[sizeof(env->queue[env->queue_tail].nick) - 1] = '\0';
        env->queue_tail = (env->queue_tail + 1) % QUEUE_SIZE;
        pthread_cond_signal(&env->queue_cond);
    } else {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
                 "Error: Command queue full for %s, command '%s' ignored", nick, cmd);
        send_to_channel(err_msg);
    }
    pthread_mutex_unlock(&env->queue_mutex);
}

Command *dequeue(Env *env) {
    if (!env || env->being_freed || env->cancel_flag) return NULL;

    pthread_mutex_lock(&env->queue_mutex);
    if (env->queue_head == env->queue_tail) {
        pthread_mutex_unlock(&env->queue_mutex);
        return NULL;
    }

    Command *cmd = malloc(sizeof(Command));
    if (!cmd) {
        pthread_mutex_unlock(&env->queue_mutex);
        return NULL;
    }

    *cmd = env->queue[env->queue_head];
    env->queue_head = (env->queue_head + 1) % QUEUE_SIZE;

    pthread_mutex_unlock(&env->queue_mutex);
    return cmd;
}

void clear_string_stack(Env *env) {
    for (int i = 0; i <= env->string_stack_top; i++) {
        free(env->string_stack[i]);
        env->string_stack[i] = NULL;
    }
    env->string_stack_top = -1;
}

static void initEnv(Env *env, const char *nick) {
    if (!env || !nick) return;
    env->loop_stack_top     = 0;
    env->being_freed        = 0;
    env->cancel_flag        = 0;
    strncpy(env->nick, nick, MAX_STRING_SIZE - 1);
    env->nick[MAX_STRING_SIZE - 1] = '\0';
    env->loop_nesting_level = 0;
    env->main_stack.top     = -1;
    env->return_stack.top   = -1;

    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(env->main_stack.data[i]);
        mpz_init(env->return_stack.data[i]);
    }
    for (int i = 0; i < MPZ_POOL_SIZE; i++) {
        mpz_init(env->mpz_pool[i]);
    }

    initDynamicDictionary(&env->dictionary);
    initWordHash(env);
    memory_init(&env->memory_list);

    unsigned long username_idx = memory_create(&env->memory_list, "USERNAME", TYPE_STRING);
    if (username_idx != 0) {
        MemoryNode *username_node = memory_get(&env->memory_list, username_idx);
        if (username_node) {
            memory_store(&env->memory_list, username_idx, nick, env);
            if (env->dictionary.count >= env->dictionary.capacity)
                resizeDynamicDictionary(&env->dictionary);
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name           = strdup("USERNAME");
            env->dictionary.words[dict_idx].code           = SAFE_MALLOC(sizeof(Instruction));
            env->dictionary.words[dict_idx].code_capacity  = 1;
            env->dictionary.words[dict_idx].code[0].opcode  = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = username_idx;
            env->dictionary.words[dict_idx].code_length    = 1;
            /* BUG 5 fix : USERNAME est un mot OP_PUSH+TYPE_STRING, il n'a pas
             * de litteraux numeriques dans strings[] — inutile d'allouer. */
            env->dictionary.words[dict_idx].strings        = NULL;
            env->dictionary.words[dict_idx].string_capacity = 0;
            env->dictionary.words[dict_idx].string_count   = 0;
            env->dictionary.words[dict_idx].immediate      = 0;
            addWordToHash(env, "USERNAME", dict_idx);
        }
    }

    env->buffer_pos             = 0;
    memset(env->output_buffer, 0, BUFFER_SIZE);
    env->currentWord.name       = NULL;
    env->currentWord.code       = NULL;
    env->currentWord.strings    = NULL;
    env->currentWord.code_capacity   = 0;
    env->currentWord.string_capacity = 0;
    env->currentWord.code_length     = 0;
    env->currentWord.string_count    = 0;
    env->compiling              = 0;
    env->current_word_index     = -1;
    env->control_stack_top      = 0;
    env->string_stack_top       = -1;
    for (int i = 0; i < STACK_SIZE; i++)
        env->string_stack[i] = NULL;
    env->error_flag         = 0;
    env->emit_buffer[0]     = '\0';
    env->emit_buffer_pos    = 0;

    env->queue_head     = 0;
    env->queue_tail     = 0;
    env->thread_running = 0;
    pthread_mutex_init(&env->queue_mutex, NULL);
    pthread_cond_init(&env->queue_cond, NULL);
    pthread_mutex_init(&env->in_use_mutex, NULL);
    env->thread = 0;
    env->next   = NULL;
}

Env *createEnv(const char *nick) {
    if (!nick) {
        send_to_channel("createEnv: Invalid nick");
        return NULL;
    }

    Env *new_env = (Env *)SAFE_MALLOC(sizeof(Env));
    if (!new_env) {
        send_to_channel("createEnv: Memory allocation failed");
        return NULL;
    }

    initEnv(new_env, nick);
    initDictionary(new_env);
    new_env->being_freed = 0;
    new_env->cancel_flag = 0;

    pthread_rwlock_wrlock(&env_rwlock);
    new_env->next = head;
    head = new_env;
    pthread_rwlock_unlock(&env_rwlock);

    new_env->thread_running = 1;
    pthread_create(&new_env->thread, NULL, env_interpret_thread, new_env);

    fprintf(stderr, "Created Env for %s\n", nick);
    return new_env;
}

void freeEnv(const char *nick) {
    if (!nick) return;

    pthread_rwlock_wrlock(&env_rwlock);
    Env *prev = NULL, *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) {
        pthread_rwlock_unlock(&env_rwlock);
        return;
    }
    curr->being_freed = 1;
    curr->cancel_flag = 1;
    if (prev) prev->next = curr->next;
    else      head       = curr->next;
    pthread_rwlock_unlock(&env_rwlock);

    pthread_mutex_lock(&curr->queue_mutex);
    if (curr->queue_head != curr->queue_tail) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
                 "Warning: Clearing pending commands for %s to free environment", nick);
        send_to_channel(err_msg);
        curr->queue_head = curr->queue_tail;
    }
    curr->thread_running = 0;
    pthread_cond_broadcast(&curr->queue_cond);
    pthread_mutex_unlock(&curr->queue_mutex);

    /*
     * CORRECTION point 6 — éviter pthread_join() sur soi-même.
     *
     * Cas 1 : thread = 0
     *   Le thread d'interprétation s'est déjà effacé (timeout auto-libération
     *   dans interpret.c : il a mis thread=0 sous mutex avant de se détacher).
     *   → Rien à joindre.
     *
     * Cas 2 : thread != 0 ET appelant != ce thread
     *   Chemin normal (QUIT utilisateur, EXIT bot, arrêt signal).
     *   → pthread_join() classique.
     *
     * Cas 3 : thread != 0 ET appelant == ce thread
     *   Ne devrait plus arriver grâce au chemin dans interpret.c, mais on
     *   garde cette garde en défense en profondeur pour éviter le deadlock.
     *   → On saute le join (le thread est ou sera détaché).
     */
    if (curr->thread != 0) {
        if (!pthread_equal(curr->thread, pthread_self())) {
            /* Cas 2 : join normal */
            if (pthread_join(curr->thread, NULL) != 0) {
                char err_msg[512];
                snprintf(err_msg, sizeof(err_msg),
                         "freeEnv: Failed to join thread for %s", nick);
                send_to_channel(err_msg);
            }
        }
        /* Cas 3 : on est le thread lui-même — on ne joint pas */
        curr->thread = 0;
    }
    /* Cas 1 : thread == 0, rien à faire */

    for (int i = 0; i < MPZ_POOL_SIZE; i++)
        mpz_clear(curr->mpz_pool[i]);
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(curr->main_stack.data[i]);
        mpz_clear(curr->return_stack.data[i]);
    }

    for (long int i = 0; i < curr->dictionary.count; i++) {
        if (curr->dictionary.words[i].name) free(curr->dictionary.words[i].name);
        for (int j = 0; j < curr->dictionary.words[i].string_count; j++) {
            if (curr->dictionary.words[i].strings[j])
                free(curr->dictionary.words[i].strings[j]);
        }
        if (curr->dictionary.words[i].code)    free(curr->dictionary.words[i].code);
        if (curr->dictionary.words[i].strings) free(curr->dictionary.words[i].strings);
    }
    if (curr->dictionary.words) free(curr->dictionary.words);

    MemoryNode *node = curr->memory_list.head;
    while (node) {
        MemoryNode *next = node->next;
        if (node->type == TYPE_VAR || node->type == TYPE_CONSTANT)
            mpz_clear(node->value.number);
        else if (node->type == TYPE_STRING && node->value.string)
            free(node->value.string);
        else if (node->type == TYPE_ARRAY && node->value.array.data) {
            for (unsigned long i = 0; i < node->value.array.size; i++)
                mpz_clear(node->value.array.data[i]);
            free(node->value.array.data);
        }
        free(node->name);
        free(node);
        node = next;
    }

    if (curr->currentWord.name) free(curr->currentWord.name);
    for (int i = 0; i < curr->currentWord.string_count; i++) {
        if (curr->currentWord.strings[i]) free(curr->currentWord.strings[i]);
    }
    if (curr->currentWord.code)    free(curr->currentWord.code);
    if (curr->currentWord.strings) free(curr->currentWord.strings);

    clear_string_stack(curr);

    if (pthread_mutex_destroy(&curr->queue_mutex) != 0) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
                 "freeEnv: Failed to destroy queue_mutex for %s", nick);
        send_to_channel(err_msg);
    }
    if (pthread_cond_destroy(&curr->queue_cond) != 0) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg),
                 "freeEnv: Failed to destroy queue_cond for %s", nick);
        send_to_channel(err_msg);
    }
if (pthread_mutex_destroy(&curr->in_use_mutex) != 0) {
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg),
             "freeEnv: Failed to destroy in_use_mutex for %s", nick);
    send_to_channel(err_msg);
}
    fprintf(stderr, "Freed Env for %s\n", nick);
    freeWordHash(curr);
    free(curr);
}

Env *findEnv(const char *nick) {
    if (!nick) return NULL;

    pthread_rwlock_rdlock(&env_rwlock);
    Env *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0)
        curr = curr->next;
    if (curr && curr->being_freed) curr = NULL;
    pthread_rwlock_unlock(&env_rwlock);
    return curr;
}
