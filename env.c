#include <pthread.h>
#include <unistd.h> 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory_forth.h"
#include "forth_bot.h"

 
pthread_mutex_t env_mutex = PTHREAD_MUTEX_INITIALIZER; // Initialize here

void initEnv(Env *env, const char *nick) {
    strncpy(env->nick, nick, MAX_STRING_SIZE - 1);
    env->nick[MAX_STRING_SIZE - 1] = '\0';

    env->main_stack.top = -1;
    env->return_stack.top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(env->main_stack.data[i]);
        mpz_init(env->return_stack.data[i]);
    }
    for (int i = 0; i < MPZ_POOL_SIZE; i++) {
        mpz_init(env->mpz_pool[i]);
    }
    initDynamicDictionary(&env->dictionary);

    memory_init(&env->memory_list);

    unsigned long username_idx = memory_create(&env->memory_list, "USERNAME", TYPE_STRING);
    if (username_idx != 0) {
        MemoryNode *username_node = memory_get(&env->memory_list, username_idx);
        if (username_node) {
            memory_store(&env->memory_list, username_idx, nick);
            if (env->dictionary.count >= env->dictionary.capacity) {
                resizeDynamicDictionary(&env->dictionary);
            }
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup("USERNAME");
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = username_idx;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            env->dictionary.words[dict_idx].immediate = 0;
        }
    }

    env->buffer_pos = 0;
    memset(env->output_buffer, 0, BUFFER_SIZE);
    env->currentWord.name = NULL;
    env->currentWord.code_length = 0;
    env->currentWord.string_count = 0;
    env->compiling = 0;
    env->current_word_index = -1;

    env->control_stack_top = 0;

    env->string_stack_top = -1;
    for (int i = 0; i < STACK_SIZE; i++) env->string_stack[i] = NULL;

    env->error_flag = 0;
    env->emit_buffer[0] = '\0';
    env->emit_buffer_pos = 0;

    // Initialize per-environment queue and thread fields
    env->queue_head = 0;
    env->queue_tail = 0;
    env->in_use = 0;
	pthread_mutex_init(&env->queue_mutex, NULL);
    pthread_cond_init(&env->queue_cond, NULL);  // Initialisation de la condition
    pthread_mutex_init(&env->in_use_mutex, NULL);  // Initialisation du mutex pour in_use
    env->thread_running = 1;
 
    env->next = NULL;
}

Env *createEnv(const char *nick) {
    Env *new_env = (Env *)malloc(sizeof(Env));
    if (!new_env) {
        send_to_channel("createEnv: Memory allocation failed");
        return NULL;
    }
    initEnv(new_env, nick);
    initDictionary(new_env);

    if (pthread_create(&new_env->thread, NULL, env_interpret_thread, new_env) != 0) {
        send_to_channel("createEnv: Failed to create interpreter thread");
        freeEnv(new_env->nick);  // Utilise freeEnv pour nettoyer proprement
        return NULL;
    }
    pthread_detach(new_env->thread);

    pthread_mutex_lock(&env_mutex);
    new_env->next = head;
    head = new_env;
    pthread_mutex_unlock(&env_mutex);
    return new_env;
}
 
void freeEnv(const char *nick) {
    Env *prev = NULL, *curr = head;

    pthread_mutex_lock(&env_mutex);
    while (curr && strcmp(curr->nick, nick) != 0) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) {
        pthread_mutex_unlock(&env_mutex);
        return;
    }
    if (prev) prev->next = curr->next;
    else head = curr->next;
    pthread_mutex_unlock(&env_mutex);

    curr->thread_running = 0;
    pthread_mutex_lock(&curr->queue_mutex);
    pthread_cond_signal(&curr->queue_cond);
    pthread_mutex_unlock(&curr->queue_mutex);

    int timeout = 500;
    while (timeout > 0) {
        pthread_mutex_lock(&curr->in_use_mutex);
        int in_use = curr->in_use;
        pthread_mutex_unlock(&curr->in_use_mutex);
        if (!in_use) break;
        usleep(10000);
        timeout--;
    }
    if (timeout <= 0) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Warning: Timeout waiting for thread to finish for %s", nick);
        send_to_channel(err_msg);
    }

    for (int i = 0; i < MPZ_POOL_SIZE; i++) {
        mpz_clear(curr->mpz_pool[i]);
    }

    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(curr->main_stack.data[i]);
        mpz_clear(curr->return_stack.data[i]);
    }

    // Libération du dictionnaire : toutes les entrées jusqu’à capacity
    for (long int i = 0; i < curr->dictionary.capacity; i++) {  // Changement ici : capacity au lieu de count
        if (curr->dictionary.words[i].name) {
            free(curr->dictionary.words[i].name);
            curr->dictionary.words[i].name = NULL;
        }
        for (int j = 0; j < curr->dictionary.words[i].string_count; j++) {
            if (curr->dictionary.words[i].strings[j]) {
                free(curr->dictionary.words[i].strings[j]);
                curr->dictionary.words[i].strings[j] = NULL;
            }
        }
        if (curr->dictionary.words[i].code) {
            free(curr->dictionary.words[i].code);
            curr->dictionary.words[i].code = NULL;
        }
        if (curr->dictionary.words[i].strings) {
            free(curr->dictionary.words[i].strings);
            curr->dictionary.words[i].strings = NULL;
        }
    }
    free(curr->dictionary.words);

    MemoryNode *node = curr->memory_list.head;
    while (node) {
        MemoryNode *next = node->next;
        memory_free(&curr->memory_list, node->name);
        node = next;
    }

    if (curr->currentWord.name) free(curr->currentWord.name);
    for (int i = 0; i < curr->currentWord.string_count; i++) {
        if (curr->currentWord.strings[i]) free(curr->currentWord.strings[i]);
    }
    if (curr->currentWord.code) free(curr->currentWord.code);
    if (curr->currentWord.strings) free(curr->currentWord.strings);

    for (int i = 0; i <= curr->string_stack_top; i++) {
        if (curr->string_stack[i]) free(curr->string_stack[i]);
    }

    pthread_mutex_destroy(&curr->queue_mutex);
    pthread_cond_destroy(&curr->queue_cond);
    pthread_mutex_destroy(&curr->in_use_mutex);

    free(curr);
}
Env *findEnv(const char *nick) {
    pthread_mutex_lock(&env_mutex);
    Env *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) curr = curr->next;
    pthread_mutex_unlock(&env_mutex);
    return curr;
}

 
