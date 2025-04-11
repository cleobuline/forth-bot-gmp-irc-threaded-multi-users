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
        mpz_set_ui(result, 0);
    }
}

void push_string(Env * env,char *str) {
    if (!env) return;
    if (env->string_stack_top < STACK_SIZE - 1) {
        env->string_stack[++env->string_stack_top] = str;
    } else {
        env->error_flag = 1;
        send_to_channel("Error: String stack overflow");
    }
}

char *pop_string(Env * env) {
    if (!env) return NULL;
    if (env->string_stack_top >= 0) {
        return env->string_stack[env->string_stack_top--];
    } else {
        env->error_flag = 1;
        send_to_channel("Error: String stack underflow");
        return NULL;
    }
}

void set_error(Env * env,const char *msg) {
    if (!env) return;
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg), "Error: %s", msg);
    send_to_channel(err_msg);
    env->error_flag = 1;
    while (env->return_stack.top >= 2 && env->return_stack.top % 3 == 0) {
        env->return_stack.top -= 3;
    }
}
