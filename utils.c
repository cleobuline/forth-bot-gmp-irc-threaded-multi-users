#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include "memory_forth.h"
#include "forth_bot.h"

// Fonctions Forth
 

void push(Env *env, mpz_t value) {
    if (env->main_stack.top >= STACK_SIZE - 1) {
        set_error(env, "Stack overflow");
        env->main_stack.top = -1; // Réinitialise la pile principale
        env->return_stack.top = -1; // Réinitialise la pile de retour
 		env->loop_nesting_level=0 ; // Niveau d'imbrication des boucles
        // env->error_flag = 0 ; 
        return;
    }
    env->main_stack.top++;
    mpz_set(env->main_stack.data[env->main_stack.top], value);
}

void pop(Env * env, mpz_t result) {
    if (env->main_stack.top >= 0) {
        mpz_set(result, env->main_stack.data[env->main_stack.top--]);
    } else {
        if (env) env->error_flag = 1;
        send_to_channel("Error: Stack underflow");
        env->loop_nesting_level=0 ; // Niveau d'imbrication des boucles
        mpz_set_ui(result, 0);
    }
}

void push_string(Env *env, char *str) {
    if (!env) return;
    pthread_mutex_lock(&env->in_use_mutex);
    if (env->string_stack_top < STACK_SIZE - 1) {
        env->string_stack[++env->string_stack_top] = str;
    } else {
        env->error_flag = 1;
        send_to_channel("Error: String stack overflow");
        free(str); // Libérer la chaîne si on ne peut pas la pousser
    }
    pthread_mutex_unlock(&env->in_use_mutex);
}

char *pop_string(Env *env) {
    if (!env) return NULL;
    pthread_mutex_lock(&env->in_use_mutex);
    if (env->string_stack_top >= 0) {
        char *str = env->string_stack[env->string_stack_top];
        env->string_stack[env->string_stack_top--] = NULL; // Remettre à NULL
        pthread_mutex_unlock(&env->in_use_mutex);
        return str;
    } else {
        env->error_flag = 1;
        send_to_channel("Error: String stack underflow");
        pthread_mutex_unlock(&env->in_use_mutex);
        return NULL;
    }
}
 
 
void set_error(Env * env,const char *msg) {
    if (!env) return;
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg), "Error: %s", msg);
    send_to_channel(err_msg);
 
    env->error_flag = 1;
 
 
    /* BUG 5 fix : nettoyer la return_stack correctement.
     * Chaque DO..LOOP empile exactement 3 valeurs (limite, index, adresse),
     * et loop_nesting_level compte le nombre de boucles actives.
     * L'ancienne condition "% 3 == 0" etait arbitraire et pouvait laisser
     * des entrees orphelines sur la pile si top % 3 != 0. */
    if (env->loop_nesting_level > 0) {
        env->return_stack.top -= env->loop_nesting_level * 3;
        if (env->return_stack.top < -1) env->return_stack.top = -1;
        env->loop_nesting_level = 0;
    }
}
