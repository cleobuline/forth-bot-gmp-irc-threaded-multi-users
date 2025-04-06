#include <pthread.h>
#include "memory_forth.h"
#include "forth_bot.h"

pthread_mutex_t env_mutex;  // Déclaration sans initialisation ici

void initEnv(Env *env, const char *nick) {
    strncpy(env->nick, nick, MAX_STRING_SIZE - 1);
    env->nick[MAX_STRING_SIZE - 1] = '\0';

    env->main_stack.top = -1;
    env->return_stack.top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_init(env->main_stack.data[i]);
        mpz_init(env->return_stack.data[i]);
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

    env->next = NULL;
}

Env *createEnv(const char *nick) {
    Env *new_env = (Env *)malloc(sizeof(Env));
    if (!new_env) {
        send_to_channel("createEnv: Memory allocation failed");
        return NULL;
    }
    initEnv(new_env, nick);
    new_env->next = head;
    head = new_env;
    return new_env;
}

void freeEnv(const char *nick) {
    Env *prev = NULL, *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) return;

    if (prev) prev->next = curr->next;
    else head = curr->next;

    pthread_mutex_lock(&env_mutex);
    if (curr == currentenv) currentenv = NULL;
    pthread_mutex_unlock(&env_mutex);

    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(curr->main_stack.data[i]);
        mpz_clear(curr->return_stack.data[i]);
    }

    for (long int i = 0; i < curr->dictionary.count; i++) {
        if (curr->dictionary.words[i].name) free(curr->dictionary.words[i].name);
        for (int j = 0; j < curr->dictionary.words[i].string_count; j++) {
            if (curr->dictionary.words[i].strings[j]) free(curr->dictionary.words[i].strings[j]);
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
    for (int i = 0; i <= curr->string_stack_top; i++) {
        if (curr->string_stack[i]) free(curr->string_stack[i]);
    }
    free(curr);
}

Env *findEnv(const char *nick) {
    Env *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) curr = curr->next;
    if (curr) {
        pthread_mutex_lock(&env_mutex);
        currentenv = curr;
        pthread_mutex_unlock(&env_mutex);
        return curr;
    }
    return NULL;
}

void set_currentenv(Env *env) {
    pthread_mutex_lock(&env_mutex);
    currentenv = env;
    pthread_mutex_unlock(&env_mutex);
}
