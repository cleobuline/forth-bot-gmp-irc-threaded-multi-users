#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory_forth.h"
#include "forth_bot.h"

pthread_mutex_t env_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex global pour la liste des environnements

// Initialise un environnement Forth pour un utilisateur donné
static void initEnv(Env *env, const char *nick) {
    if (!env || !nick) return;

    // Initialisation des champs de base
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

    // Initialisation des structures dynamiques
    initDynamicDictionary(&env->dictionary);
    memory_init(&env->memory_list);

    // Ajout de la variable USERNAME
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

    // Initialisation des buffers et états
    env->buffer_pos = 0;
    memset(env->output_buffer, 0, BUFFER_SIZE);
    env->currentWord.name = NULL;
    env->currentWord.code_length = 0;
    env->currentWord.string_count = 0;
    env->compiling = 0;
    env->current_word_index = -1;
    env->control_stack_top = 0;
    env->string_stack_top = -1;
    for (int i = 0; i < STACK_SIZE; i++) {
        env->string_stack[i] = NULL;
    }
    env->error_flag = 0;
    env->emit_buffer[0] = '\0';
    env->emit_buffer_pos = 0;

    // Initialisation des champs pour la queue et le thread
    env->queue_head = 0;
    env->queue_tail = 0;
    env->in_use = 0;
    env->thread_running = 0; // Thread pas encore démarré
    pthread_mutex_init(&env->queue_mutex, NULL);
    pthread_cond_init(&env->queue_cond, NULL);
    pthread_mutex_init(&env->in_use_mutex, NULL);
    env->thread = 0; // Thread non défini
    env->next = NULL;
}

// Crée un nouvel environnement et le démarre
Env *createEnv(const char *nick) {
    if (!nick) {
        send_to_channel("createEnv: Invalid nick");
        return NULL;
    }

    // Allocation et initialisation
    Env *new_env = (Env *)malloc(sizeof(Env));
    if (!new_env) {
        send_to_channel("createEnv: Memory allocation failed");
        return NULL;
    }
    initEnv(new_env, nick);
    initDictionary(new_env);

    // Démarrage du thread d'interprétation
    new_env->thread_running = 1;
    if (pthread_create(&new_env->thread, NULL, env_interpret_thread, new_env) != 0) {
        send_to_channel("createEnv: Failed to create interpreter thread");
        freeEnv(new_env->nick); // Nettoyage complet en cas d'échec
        return NULL;
    }
    pthread_detach(new_env->thread); // Thread détaché, pas besoin de join explicite

    // Ajout à la liste chaînée globale
    pthread_mutex_lock(&env_mutex);
    new_env->next = head;
    head = new_env;
    pthread_mutex_unlock(&env_mutex);

    return new_env;
}

// Libère un environnement de manière sécurisée
void freeEnv(const char *nick) {
    if (!nick) return;

    // Recherche de l'environnement dans la liste
    Env *prev = NULL, *curr = head;
    pthread_mutex_lock(&env_mutex);
    while (curr && strcmp(curr->nick, nick) != 0) {
        prev = curr;
        curr = curr->next;
    }
    if (!curr) {
        pthread_mutex_unlock(&env_mutex);
        return; // Environnement non trouvé
    }
    if (prev) prev->next = curr->next;
    else head = curr->next;
    pthread_mutex_unlock(&env_mutex);

    // Arrêt du thread
    curr->thread_running = 0;
    pthread_mutex_lock(&curr->queue_mutex);
    pthread_cond_signal(&curr->queue_cond); // Réveille le thread pour qu'il s'arrête
    pthread_mutex_unlock(&curr->queue_mutex);

    // Attente de la fin du thread avec timeout
    int timeout_ms = 500;
    while (timeout_ms > 0) {
        pthread_mutex_lock(&curr->in_use_mutex);
        int in_use = curr->in_use;
        pthread_mutex_unlock(&curr->in_use_mutex);
        if (!in_use) break;
        usleep(10000); // Attente de 10ms
        timeout_ms -= 10;
    }
    if (timeout_ms <= 0) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "freeEnv: Timeout waiting for thread to finish for %s", nick);
        send_to_channel(err_msg);
    }

    // Libération des ressources GMP
    for (int i = 0; i < MPZ_POOL_SIZE; i++) {
        mpz_clear(curr->mpz_pool[i]);
    }
    for (int i = 0; i < STACK_SIZE; i++) {
        mpz_clear(curr->main_stack.data[i]);
        mpz_clear(curr->return_stack.data[i]);
    }

    // Libération du dictionnaire
    for (long int i = 0; i < curr->dictionary.count; i++) {
        free(curr->dictionary.words[i].name);
        for (int j = 0; j < curr->dictionary.words[i].string_count; j++) {
            free(curr->dictionary.words[i].strings[j]);
        }
        free(curr->dictionary.words[i].code);
        free(curr->dictionary.words[i].strings);
    }
    free(curr->dictionary.words);

    // Libération de la liste mémoire
    MemoryNode *node = curr->memory_list.head;
    while (node) {
        MemoryNode *next = node->next;
        memory_free(&curr->memory_list, node->name);
        node = next;
    }

    // Libération de currentWord
    free(curr->currentWord.name);
    for (int i = 0; i < curr->currentWord.string_count; i++) {
        free(curr->currentWord.strings[i]);
    }
    free(curr->currentWord.code);
    free(curr->currentWord.strings);

    // Libération de string_stack
    for (int i = 0; i <= curr->string_stack_top; i++) {
        free(curr->string_stack[i]);
    }

    // Destruction des primitives de synchronisation
    pthread_mutex_destroy(&curr->queue_mutex);
    pthread_cond_destroy(&curr->queue_cond);
    pthread_mutex_destroy(&curr->in_use_mutex);

    // Libération finale
    free(curr);
}

// Trouve un environnement par son nick
Env *findEnv(const char *nick) {
    if (!nick) return NULL;

    pthread_mutex_lock(&env_mutex);
    Env *curr = head;
    while (curr && strcmp(curr->nick, nick) != 0) {
        curr = curr->next;
    }
    pthread_mutex_unlock(&env_mutex);
    return curr;
}
