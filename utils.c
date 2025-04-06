#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include "memory_forth.h"
#include "forth_bot.h"

// Fonctions Forth
void push(Stack *stack, mpz_t value) {
    if (stack->top < STACK_SIZE - 1) {
        mpz_set(stack->data[++stack->top], value);
    } else {
        if (currentenv) currentenv->error_flag = 1;
        send_to_channel("Error: Stack overflow");
    }
}

void pop(Stack *stack, mpz_t result) {
    if (stack->top >= 0) {
        mpz_set(result, stack->data[stack->top--]);
    } else {
        if (currentenv) currentenv->error_flag = 1;
        send_to_channel("Error: Stack underflow");
        mpz_set_ui(result, 0);
    }
}

void push_string(char *str) {
    if (!currentenv) return;
    if (currentenv->string_stack_top < STACK_SIZE - 1) {
        currentenv->string_stack[++currentenv->string_stack_top] = str;
    } else {
        currentenv->error_flag = 1;
        send_to_channel("Error: String stack overflow");
    }
}

char *pop_string() {
    if (!currentenv) return NULL;
    if (currentenv->string_stack_top >= 0) {
        return currentenv->string_stack[currentenv->string_stack_top--];
    } else {
        currentenv->error_flag = 1;
        send_to_channel("Error: String stack underflow");
        return NULL;
    }
}

void set_error(const char *msg) {
    if (!currentenv) return;
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg), "Error: %s", msg);
    send_to_channel(err_msg);
    currentenv->error_flag = 1;
    while (currentenv->return_stack.top >= 2 && currentenv->return_stack.top % 3 == 0) {
        currentenv->return_stack.top -= 3;
    }
}
