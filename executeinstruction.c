#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>  // Ajouté pour usleep
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <time.h>
#include <ctype.h>
#include "memory_forth.h"
#include <netdb.h>
#include <curl/curl.h>
#include "forth_bot.h"

void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index, Env *env) {
    if (!env || env->error_flag) return;
    mpz_t *a = &env->mpz_pool[0], *b = &env->mpz_pool[1], *result = &env->mpz_pool[2];
 
unsigned long encoded_idx;
    unsigned long type;
    MemoryNode *node;
        struct timeval tv;
    switch (instr.opcode) {
 
case OP_PUSH:
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        if (mpz_set_str(*result, word->strings[instr.operand], 10) == 0) {
            push(env, *result);
        } else {
            set_error(env,"OP_PUSH: Invalid string number");
        }
    } else {
        mpz_set_ui(*result, instr.operand);
        push(env, *result);
    }

    break;
        case OP_ADD:
            pop(env, *b);
            pop(env, *a);
            mpz_add(*result, *a, *b);
            push(env, *result);
            break;
        case OP_SUB:
            pop(env, *b);
            pop(env, *a);
            mpz_sub(*result, *a, *b);
            push(env, *result);
            break;
        case OP_MUL:
            pop(env, *b);
            pop(env, *a);
            mpz_mul(*result, *a, *b);
            push(env, *result);
            break;
        case OP_DIV:
            pop(env, *b);
            pop(env, *a);
            if (mpz_cmp_ui(*b, 0) == 0) {
                set_error(env,"Division by zero");
                push(env, *a);
                push(env, *b);
            } else {
                mpz_div(*result, *a, *b);
                push(env, *result);
            }
            break;
        case OP_MOD:
            pop(env, *b);
            pop(env, *a);
            if (mpz_cmp_ui(*b, 0) == 0) {
                set_error(env,"Modulo by zero");
                push(env, *a);
                push(env, *b);
            } else {
                mpz_mod(*result, *a, *b);
                push(env, *result);
            }
            break;
            
        case OP_DUP:
            if (stack->top >= 0) push(env, stack->data[stack->top]);
            else set_error(env,"DUP: Stack underflow");
            break;
        case OP_DROP:
            pop(env, *a);
            break;
        case OP_SWAP:
            if (stack->top >= 1) {
                mpz_set(*a, stack->data[stack->top]);
                mpz_set(stack->data[stack->top], stack->data[stack->top - 1]);
                mpz_set(stack->data[stack->top - 1], *a);
            } else set_error(env,"SWAP: Stack underflow");
            break;
        case OP_OVER:
            if (stack->top >= 1) push(env, stack->data[stack->top - 1]);
            else set_error(env,"OVER: Stack underflow");
            break;
        case OP_ROT:
            if (stack->top >= 2) {
                mpz_set(*a, stack->data[stack->top]);
                mpz_set(stack->data[stack->top], stack->data[stack->top - 2]);
                mpz_set(stack->data[stack->top - 2], stack->data[stack->top - 1]);
                mpz_set(stack->data[stack->top - 1], *a);
            } else set_error(env,"ROT: Stack underflow");
            break;
case OP_TO_R:
    pop(env, *a);
    if (!env->error_flag) {
        if (env->return_stack.top < STACK_SIZE - 1) {
            env->return_stack.top++;
            mpz_set(env->return_stack.data[env->return_stack.top], *a);
            //char debug_msg[512];
            //snprintf(debug_msg, sizeof(debug_msg), ">R: Pushed value %s, return_stack_top=%ld", mpz_get_str(NULL, 10, *a), env->return_stack.top);
            //send_to_channel(debug_msg);
        } else {
            char debug_msg[512];
            snprintf(debug_msg, sizeof(debug_msg), ">R: Return stack overflow, top=%ld, STACK_SIZE=%d", env->return_stack.top, STACK_SIZE);
            set_error(env, debug_msg);
            push(env, *a);
        }
    }
    break;
        case OP_FROM_R:
            if (env->return_stack.top >= 0) {
                mpz_set(*a, env->return_stack.data[env->return_stack.top--]);
                push(env, *a);
            } else set_error(env,"R>: Return stack underflow");
            break;
        case OP_R_FETCH:
            if (env->return_stack.top >= 0) push(env, env->return_stack.data[env->return_stack.top]);
            else set_error(env,"R@: Return stack underflow");
            break;
        case OP_SEE:
       
    if (env->compiling || word_index >= 0) { // Mode compilé ou dans une définition
        print_word_definition_irc(instr.operand, stack,env);
    } else { // Mode immédiat
        pop(env, *a);
        if (!env->error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < env->dictionary.count) {
            print_word_definition_irc(mpz_get_si(*a), stack,env);
        } else if (!env->error_flag) {
            set_error(env,"SEE: Invalid word index");
        }
    }  
    break;
    case OP_2DROP:
    if (stack->top >= 1) {
        pop(env, env->mpz_pool[0]); // Dépile le premier élément
        pop(env, env->mpz_pool[0]); // Dépile le second élément
    } else {
        set_error(env,"2DROP: Stack underflow");
    }
    break;
        case OP_EQ:
            pop(env, *b);
            pop(env, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) == 0);
            push(env, *result);
            break;
        case OP_LT:
            pop(env, *b);
            pop(env, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) < 0);
            push(env, *result);
            break;
        case OP_GT:
            pop(env, *b);
            pop(env, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) > 0);
            push(env, *result);
            break;
case OP_AND:
    pop(env, *b);
    pop(env, *a);
    mpz_set_si(*result, (mpz_cmp_ui(*a, 0) != 0) && (mpz_cmp_ui(*b, 0) != 0));
    push(env, *result);
    break;
case OP_OR:
    pop(env, *b);
    pop(env, *a);
    mpz_set_si(*result, (mpz_cmp_ui(*a, 0) != 0) || (mpz_cmp_ui(*b, 0) != 0));
    push(env, *result);
    break;
        case OP_NOT:
            pop(env, *a);
            mpz_set_si(*result, mpz_cmp_ui(*a, 0) == 0);
            push(env, *result);
            break;
        case OP_XOR:
        pop(env, *b);
        pop(env, *a);
        int a_true = (mpz_cmp_ui(*a, 0) != 0);
        int b_true = (mpz_cmp_ui(*b, 0) != 0);
        mpz_set_si(*result, (a_true != b_true)); // Vrai si exactement une des deux est vraie
        push(env, *result);
        break;
case OP_CALL:
    if (env->return_stack.top >= STACK_SIZE - 1) {
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg), "CALL: Return stack overflow, top=%ld, STACK_SIZE=%d", env->return_stack.top, STACK_SIZE);
        set_error(env, debug_msg);
        env->main_stack.top = -1;
        env->return_stack.top = -1;
        break;
    }
    if (instr.operand >= 0 && instr.operand < env->dictionary.count) {
        // char debug_msg[512];
        //snprintf(debug_msg, sizeof(debug_msg), "CALL: Word %s, return_stack_top=%ld", env->dictionary.words[instr.operand].name, env->return_stack.top);
        //send_to_channel(debug_msg);
        executeCompiledWord(&env->dictionary.words[instr.operand], stack, instr.operand, env);
    } else {
        set_error(env, "Invalid word index");
    }
    break;
        case OP_BRANCH:
            *ip = instr.operand - 1;
            break;
        case OP_BRANCH_FALSE:
            pop(env, *a);
            if (mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_END:
            *ip = word->code_length;
            break;
case OP_DOT:
            pop(env, *a);
            // Utiliser un buffer dynamique pour la chaîne
            char *num_str = mpz_get_str(NULL, 10, *a);
            if (!num_str) {
                set_error(env, "DOT: Memory allocation failed");
                break;
            }
            // Envoyer la chaîne via send_to_channel, qui gère le découpage
            send_to_channel(num_str);
            free(num_str);
            break;
 
case OP_DOT_S:
    if (stack->top >= 0) {
        // Utiliser un buffer dynamique pour stack_str
        char *stack_str = (char *)malloc(1024);
        if (!stack_str) {
            set_error(env, "DOT_S: Memory allocation failed");
            break;
        }
        strcpy(stack_str, "<");
        size_t stack_str_len = 1;
        size_t stack_str_capacity = 1024;

        // Ajouter la taille de la pile
        char num_str[32];
        snprintf(num_str, sizeof(num_str), "%ld", stack->top + 1);
        size_t num_len = strlen(num_str);
        if (stack_str_len + num_len + 2 >= stack_str_capacity) {
            stack_str_capacity *= 2;
            stack_str = (char *)SAFE_REALLOC(stack_str, stack_str_capacity);
            if (!stack_str) {
                set_error(env, "DOT_S: Memory allocation failed");
                break;
            }
        }
        strcat(stack_str, num_str);
        strcat(stack_str, "> ");
        stack_str_len += num_len + 2;

        // Ajouter chaque élément de la pile
        for (int i = 0; i <= stack->top; i++) {
            // Utiliser mpz_get_str avec un buffer dynamique
            char *num_str_dynamic = mpz_get_str(NULL, 10, stack->data[i]);
            if (!num_str_dynamic) {
                free(stack_str);
                set_error(env, "DOT_S: Memory allocation failed");
                break;
            }
            num_len = strlen(num_str_dynamic);
            if (stack_str_len + num_len + 2 >= stack_str_capacity) {
                stack_str_capacity = stack_str_len + num_len + 1024;
                stack_str = (char *)SAFE_REALLOC(stack_str, stack_str_capacity);
                if (!stack_str) {
                    free(num_str_dynamic);
                    set_error(env, "DOT_S: Memory allocation failed");
                    break;
                }
            }
            strcat(stack_str, num_str_dynamic);
            stack_str_len += num_len;
            if (i < stack->top) {
                strcat(stack_str, " ");
                stack_str_len++;
            }
            free(num_str_dynamic);
        }
        send_to_channel(stack_str);
        free(stack_str);
    } else {
        send_to_channel("<0>");
    }
    break;
case OP_EMIT:
    if (stack->top >= 0) {
        pop(env, *a);
        char c = (char)mpz_get_si(*a);
        if (env->buffer_pos < BUFFER_SIZE - 1) {
            env->output_buffer[env->buffer_pos++] = c;
            env->output_buffer[env->buffer_pos] = '\0';  // Terminer la chaîne
        } else {
            set_error(env,"Output buffer overflow");
        }
    } else {
        set_error(env,"EMIT: Stack underflow");
    }
    break;

case OP_CR:
    if (env->buffer_pos < BUFFER_SIZE - 1) {
        env->output_buffer[env->buffer_pos++] = '\n';
        env->output_buffer[env->buffer_pos] = '\0';
    }
    if (env->buffer_pos > 0) {
        send_to_channel(env->output_buffer);  // Envoyer au canal IRC
        env->buffer_pos = 0;  // Réinitialiser le buffer
        memset(env->output_buffer, 0, BUFFER_SIZE);
    }
    break;
case OP_VARIABLE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        unsigned long index = memory_create(&env->memory_list, name, TYPE_VAR);
        if (index == 0) {
            set_error(env, "VARIABLE: Memory creation failed");
            break;
        }
        if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
        int dict_idx = env->dictionary.count++;
        env->dictionary.words[dict_idx].name = strdup(name);
        env->dictionary.words[dict_idx].code = SAFE_MALLOC(sizeof(Instruction));
        if (!env->dictionary.words[dict_idx].name || !env->dictionary.words[dict_idx].code) {
            set_error(env, "VARIABLE: Memory allocation failed");
            free(env->dictionary.words[dict_idx].name);
            free(env->dictionary.words[dict_idx].code);
            break;
        }
        env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
        env->dictionary.words[dict_idx].code[0].operand = index;
        env->dictionary.words[dict_idx].code_length = 1;
        env->dictionary.words[dict_idx].strings = SAFE_MALLOC(sizeof(char *));
        env->dictionary.words[dict_idx].string_capacity = 1;
        env->dictionary.words[dict_idx].string_count = 0;
        env->dictionary.words[dict_idx].immediate = 0;
    } else {
        set_error(env, "VARIABLE: Invalid name");
    }
    break;
case OP_STORE:
    if (stack->top < 0) {
        set_error(env,"STORE: Stack underflow for address");
        break;
    }
    char debug_msg[512];
    /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Starting, stack_top=%d", stack->top);
    send_to_channel(debug_msg);
 */
    pop(env, *result); // encoded_idx
    encoded_idx = mpz_get_ui(*result);
   /*   snprintf(debug_msg, sizeof(debug_msg), "STORE: Popped encoded_idx=%lu, stack_top=%d", encoded_idx, stack->top);
    send_to_channel(debug_msg);
 */
    node = memory_get(&env->memory_list, encoded_idx);
    if (!node) {
        snprintf(debug_msg, sizeof(debug_msg), "STORE: Invalid memory index for encoded_idx=%lu", encoded_idx);
        set_error(env,debug_msg);
        push(env, *result);
        break;
    }
    type = node->type;
     /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Found node %s with type=%lu, stack_top=%d", node->name, type, stack->top);
    send_to_channel(debug_msg);
 */
    if (type == TYPE_VAR) {
        if (stack->top < 0) {
            set_error(env,"STORE: Stack underflow for variable value");
            push(env, *result);
            break;
        }
        pop(env, *a);
        memory_store(&env->memory_list, encoded_idx, a);
        /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s = %s", node->name, mpz_get_str(NULL, 10, *a));
        send_to_channel(debug_msg);
        */
         
    } else if (type == TYPE_ARRAY) {
        if (stack->top < 1) {
            set_error(env,"STORE: Stack underflow for array operation");
            push(env, *result);
            break;
        }
        pop(env, *a); // offset
        pop(env, *b); // valeur
        int offset = mpz_get_si(*a);
         /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Array operation on %s, offset=%d, value=%s", node->name, offset, mpz_get_str(NULL, 10, *b));
        send_to_channel(debug_msg);
        */
        if (offset >= 0 && offset < node->value.array.size) {
            mpz_set(node->value.array.data[offset], *b);
            /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s[%d] = %s", node->name, offset, mpz_get_str(NULL, 10, *b));
            send_to_channel(debug_msg);
            */ 
        } else {
            snprintf(debug_msg, sizeof(debug_msg), "STORE: Array index %d out of bounds (size=%lu)", offset, node->value.array.size);
            set_error(env,debug_msg);
            push(env, *b);
            push(env, *a);
            push(env, *result);
        }
    } else if (type == TYPE_STRING) {
        if (stack->top < 0) {
            set_error(env,"STORE: Stack underflow for string value");
            push(env, *result);
            break;
        }
        pop(env, *a); // index dans string_stack
        int str_idx = mpz_get_si(*a);
        if (mpz_fits_slong_p(*a) && str_idx >= 0 && str_idx <= env->string_stack_top) {
            char *str = env->string_stack[str_idx];
            if (str) {
                memory_store(&env->memory_list, encoded_idx, str);
                for (int i = str_idx; i < env->string_stack_top; i++) {
                    env->string_stack[i] = env->string_stack[i + 1];
                }
                env->string_stack[env->string_stack_top--] = NULL;
                /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s = %s", node->name, str);
                send_to_channel(debug_msg);
                */ 
            } else {
                set_error(env,"STORE: No string at stack index");
                push(env, *a);
                push(env, *result);
            }
        } else {
            set_error(env,"STORE: Invalid string stack index");
            push(env, *a);
            push(env, *result);
        }
    } else {
        set_error(env,"STORE: Unknown type");
        push(env, *result);
    }
    break;
 
case OP_FETCH:
    if (stack->top < 0) {
        set_error(env,"FETCH: Stack underflow for address");
        break;
    }
    pop(env, *result); // encoded_idx (ex. ZOZO = 268435456)
    encoded_idx = mpz_get_ui(*result);
    // char debug_msg[512];
    /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Popped encoded_idx=%lu", encoded_idx);
    send_to_channel(debug_msg);
*/
    node = memory_get(&env->memory_list, encoded_idx);
    if (!node) {
        set_error(env,"FETCH: Invalid memory index");
        push(env, *result);
        break;
    }
    type = node->type; // Type réel du nœud
    /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Found %s, type=%lu, stack_top=%d", node->name, type, stack->top);
    send_to_channel(debug_msg);
*/
    if (type == TYPE_VAR) {
        memory_fetch(&env->memory_list, encoded_idx, result);
        /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Got variable %s = %s", node->name, mpz_get_str(NULL, 10, *result));
        send_to_channel(debug_msg);
        */
        push(env, *result);
    } else if (type == TYPE_ARRAY) {
        if (stack->top < 0) {
            set_error(env,"FETCH: Stack underflow for array offset");
            push(env, *result);
            break;
        }
        pop(env, *a); // offset (ex. 5)
        int offset = mpz_get_si(*a);
        /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Array offset=%d", offset);
        send_to_channel(debug_msg);
        */
        if (offset >= 0 && offset < node->value.array.size) {
            mpz_set(*result, node->value.array.data[offset]);
            /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Got %s[%d] = %s", node->name, offset, mpz_get_str(NULL, 10, *result));
            send_to_channel(debug_msg);
            */ 
            push(env, *result);
        } else {
            snprintf(debug_msg, sizeof(debug_msg), "FETCH: Array index %d out of bounds (size=%lu)", offset, node->value.array.size);
            set_error(env,debug_msg);
            push(env, *a);
            push(env, *result);
        }
    } else if (type == TYPE_STRING) {
        char *str;
        memory_fetch(&env->memory_list, encoded_idx, &str);
        if (str) {
            push_string(env, strdup(str));
            free(str);
            mpz_set_si(*result, env->string_stack_top);
            push(env, *result);
        } else {
            push_string(env,NULL);
            mpz_set_si(*result, env->string_stack_top);
            push(env, *result);
        }
    } else {
        set_error(env,"FETCH: Unknown type");
        push(env, *result);
    }
    break;
    
 
 
 case OP_ALLOT:
    if (stack->top < 1) { // Vérifie 2 éléments
        set_error(env,"ALLOT: Stack underflow");
        break;
    }
    pop(env, *a); // Taille
    pop(env, *result); // encoded_idx
    encoded_idx = mpz_get_ui(*result);
    node = memory_get(&env->memory_list, encoded_idx);
    if (!node) {
        set_error(env,"ALLOT: Invalid memory index");
        push(env, *result);
        push(env, *a);
        break;
    }
    if (node->type != TYPE_ARRAY) {
        set_error(env,"ALLOT: Must be an array");
        push(env, *result);
        push(env, *a);
        break;
    }
    int size = mpz_get_si(*a);
    if (size < 0) { // Accepte 0, mais négatif interdit
        set_error(env,"ALLOT: Size must be non-negative");
        push(env, *result);
        push(env, *a);
        break;
    }
    unsigned long new_size = node->value.array.size + size;
    mpz_t *new_array = SAFE_REALLOC(node->value.array.data, new_size * sizeof(mpz_t));
    if (!new_array) {
        set_error(env,"ALLOT: Memory allocation failed");
        push(env, *result);
        push(env, *a);
        break;
    }
    node->value.array.data = new_array;
    for (unsigned long i = node->value.array.size; i < new_size; i++) {
        mpz_init_set_ui(node->value.array.data[i], 0);
    }
    node->value.array.size = new_size;
    break;
 
case OP_DO:
    if (stack->top < 1) {
        set_error(env,"DO: Stack underflow");
        break;
    }
    pop(env, *b); // index initial
    pop(env, *a); // limite
    if (env->return_stack.top + 3 >= STACK_SIZE) {
        set_error(env,"DO: Return stack overflow");
        push(env, *a);
        push(env, *b);
        break;
    }
    env->return_stack.top++;
    mpz_set(env->return_stack.data[env->return_stack.top], *a); // limite
    env->return_stack.top++;
    mpz_set(env->return_stack.data[env->return_stack.top], *b); // index
    env->return_stack.top++;
    mpz_set_si(env->return_stack.data[env->return_stack.top], *ip + 1); // adresse de retour
    break;

case OP_LOOP:
    if (env->return_stack.top < 2) {
        set_error(env,"LOOP: Return stack underflow");
        break;
    }
    mpz_add_ui(*result, env->return_stack.data[env->return_stack.top - 1], 1); // index + 1
    if (mpz_cmp(*result, env->return_stack.data[env->return_stack.top - 2]) < 0) { // index < limit
        mpz_set(env->return_stack.data[env->return_stack.top - 1], *result);
        *ip = instr.operand - 1; // Retour à l'instruction après DO
    } else {
        env->return_stack.top -= 3; // Dépiler limit, index, addr
    }
    break;

case OP_I:
    if (env->return_stack.top < 1) {
        set_error(env,"I: Return stack underflow");
        break;
    }
    push(env, env->return_stack.data[env->return_stack.top - 1]); // Pousse l'index actuel
    break;

case OP_J:
    if (env->return_stack.top >= 1) {
        if (env->return_stack.top >= 4) {
            // Boucle imbriquée : lit l’indice de la boucle externe
            push(env, env->return_stack.data[env->return_stack.top - 4]);
        } else {
            // Boucle externe seule : lit l’indice courant
            push(env, env->return_stack.data[env->return_stack.top - 1]);
        }
    } else {
        set_error(env,"J: No outer loop");
    }
    break;
        case OP_UNLOOP:
    if (env->return_stack.top >= 2) {
        env->return_stack.top -= 3;  // Dépile limit, index, addr
    } else {
        set_error(env,"UNLOOP without DO");
    }
    break;
case OP_PLUS_LOOP:
    if (stack->top < 0 || env->return_stack.top < 2) {
        set_error(env,"+LOOP: Stack or return stack underflow");
        break;
    }
    pop(env, *a); // Pas
    mpz_t *index = &env->return_stack.data[env->return_stack.top - 1]; // Prendre l'adresse
    mpz_t *limit = &env->return_stack.data[env->return_stack.top - 2]; // Prendre l'adresse
    mpz_add(*index, *index, *a); // index += pas
    if (mpz_sgn(*a) >= 0) {
        if (mpz_cmp(*index, *limit) < 0) {
            *ip = instr.operand - 1; // Retour à DO
        } else {
            env->return_stack.top -= 3;
        }
    } else {
        if (mpz_cmp(*index, *limit) > 0) {
            *ip = instr.operand - 1; // Retour à DO
        } else {
            env->return_stack.top -= 3;
        }
    }
    break;
        case OP_SQRT:
            pop(env, *a);
            if (mpz_cmp_ui(*a, 0) < 0) {
                set_error(env,"Square root of negative number");
                push(env, *a);
            } else {
                mpz_sqrt(*result, *a);
                push(env, *result);
            }
        break ; 
case OP_DOT_QUOTE:
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        size_t len = strlen(word->strings[instr.operand]);
        if (env->buffer_pos + len >= BUFFER_SIZE - 1) {
            send_to_channel(env->output_buffer);
            env->buffer_pos = 0;
            memset(env->output_buffer, 0, BUFFER_SIZE);
        }
        strncpy(env->output_buffer + env->buffer_pos, word->strings[instr.operand], BUFFER_SIZE - env->buffer_pos - 1);
        env->buffer_pos += len;
        env->output_buffer[env->buffer_pos] = '\0';
    } else {
        set_error(env, "DOT_QUOTE: Invalid string index");
    }
    break;
    /*
    case OP_DOT_QUOTE: OLD VERSION 
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        size_t len = strlen(word->strings[instr.operand]);
        if (env->buffer_pos + len < BUFFER_SIZE - 1) {
            strncpy(env->output_buffer + env->buffer_pos, word->strings[instr.operand], len);
            env->buffer_pos += len;
            env->output_buffer[env->buffer_pos] = '\0';
        } else {
            set_error(env,"DOT_QUOTE: Output buffer overflow");
        }
    } else {
        set_error(env,"DOT_QUOTE: Invalid string index");
    }
    break;
 */
        case OP_CASE:
            if (env->control_stack_top < CONTROL_STACK_SIZE) {
                env->control_stack[env->control_stack_top].type = CT_CASE;
                env->control_stack[env->control_stack_top].addr = *ip;
                env->control_stack_top++;
            } else set_error(env,"Control stack overflow");
            break;
        case OP_OF:
            pop(env, *a);
            pop(env, *b);
            if (mpz_cmp(*a, *b) != 0) {
                *ip = instr.operand - 1; // Sauter à ENDOF
                push(env, *b); // Remettre la valeur de test
            } else {
                push(env, *b); // Remettre la valeur de test
            }
            break;
        case OP_ENDOF:
            *ip = instr.operand - 1; // Sauter à ENDCASE ou prochain ENDOF
            break;
        case OP_ENDCASE:
            pop(env, *a); // Dépiler la valeur de test
            if (env->control_stack_top > 0 && env->control_stack[env->control_stack_top - 1].type == CT_CASE) {
                env->control_stack_top--;
            } else set_error(env,"ENDCASE without CASE");
            break;
        case OP_EXIT:
            *ip = word->code_length;
            break;
case OP_BEGIN:
/*
     snprintf(debug_msg, sizeof(debug_msg), "BEGIN: control_stack_top=%ld, CONTROL_STACK_SIZE=%d", env->control_stack_top, CONTROL_STACK_SIZE);
            send_to_channel(debug_msg);
            
    if (env->control_stack_top < CONTROL_STACK_SIZE) {
        env->control_stack[env->control_stack_top++] = (ControlEntry){CT_BEGIN, *ip};
    } else set_error(env,"Control stack overflow");`*/
    break;
case OP_WHILE:
    pop(env, *a);
    if (!env->error_flag && mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1; // Sauter si faux
    break;
case OP_REPEAT:
    *ip = instr.operand - 1; // Toujours sauter à BEGIN, pas besoin de vérifier control_stack ici
    break;
        case OP_UNTIL:
            pop(env, *a);
            if (mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_AGAIN:
            *ip = instr.operand - 1;
            break;
        case OP_BIT_AND:
            pop(env, *b);
            pop(env, *a);
            mpz_and(*result, *a, *b);
            push(env, *result);
            break;
        case OP_BIT_OR:
            pop(env, *b);
            pop(env, *a);
            mpz_ior(*result, *a, *b);
            push(env, *result);
            break;
        case OP_BIT_XOR:
            pop(env, *b);
            pop(env, *a);
            mpz_xor(*result, *a, *b);
            push(env, *result);
            break;
        case OP_BIT_NOT:
            pop(env, *a);
            mpz_com(*result, *a);
            push(env, *result);
            break;
        case OP_LSHIFT:
            pop(env, *b);
            pop(env, *a);
            mpz_mul_2exp(*result, *a, mpz_get_ui(*b));
            push(env, *result);
            break;
        case OP_RSHIFT:
            pop(env, *b);
            pop(env, *a);
            mpz_fdiv_q_2exp(*result, *a, mpz_get_ui(*b));
            push(env, *result);
            break;
case OP_FORGET:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *word_to_forget = word->strings[instr.operand];
        int forget_idx = findCompiledWordIndex(word_to_forget,env);
        if (forget_idx >= 0) {
            // Parcourir tous les mots à partir de forget_idx jusqu’à la fin du dictionnaire
            for (int i = forget_idx; i < env->dictionary.count; i++) {
                CompiledWord *dict_word = &env->dictionary.words[i];

                // Vérifier si le mot est une variable, une chaîne ou un tableau et libérer la mémoire associée
                if (dict_word->code_length == 1 && dict_word->code[0].opcode == OP_PUSH) {
                    unsigned long encoded_idx = dict_word->code[0].operand;
                    unsigned long type = memory_get_type(encoded_idx);
                    if (type == TYPE_VAR || type == TYPE_STRING || type == TYPE_ARRAY) {
                        memory_free(&env->memory_list, dict_word->name);
                    }
                }

                // Libérer le nom du mot
                if (dict_word->name) {
                    free(dict_word->name);
                    dict_word->name = NULL;
                }

                // Libérer toutes les chaînes dans le tableau strings
                for (int j = 0; j < dict_word->string_count; j++) {
                    if (dict_word->strings[j]) {
                        free(dict_word->strings[j]);
                        dict_word->strings[j] = NULL;
                    }
                }

                // Libérer les tableaux dynamiques code et strings
                if (dict_word->code) {
                    free(dict_word->code);
                    dict_word->code = NULL;
                }
                if (dict_word->strings) {
                    free(dict_word->strings);
                    dict_word->strings = NULL;
                }

                // Réinitialiser les compteurs et capacités
                dict_word->code_length = 0;
                dict_word->code_capacity = 0;
                dict_word->string_count = 0;
                dict_word->string_capacity = 0;
                dict_word->immediate = 0;
            }

            // Mettre à jour le nombre de mots dans le dictionnaire
            long int old_dict_count = env->dictionary.count;
            env->dictionary.count = forget_idx;

            // Envoyer un message de confirmation
            char msg[512];
            snprintf(msg, sizeof(msg), "Forgot everything from '%s' at index %d (dict was %ld, now %ld; mem count now %lu)", 
                     word_to_forget, forget_idx, old_dict_count, env->dictionary.count, env->memory_list.count);
            send_to_channel(msg);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "FORGET: Unknown word: %s", word_to_forget);
            set_error(env,msg);
        }
    } else {
        set_error(env,"FORGET: Invalid word name");
    }
    break;
case OP_WORDS:
    if (env->dictionary.count > 0) {
        char words_msg[2048] = "";
        size_t remaining = sizeof(words_msg) - 1;
        for (int i = 0; i < env->dictionary.count && remaining > 1; i++) {
            if (env->dictionary.words[i].name) {
                size_t name_len = strlen(env->dictionary.words[i].name);
                if (name_len + 1 < remaining) {
                    strncat(words_msg, env->dictionary.words[i].name, remaining);
                    strncat(words_msg, " ", remaining - name_len);
                    remaining -= (name_len + 1);
                } else {
                    send_to_channel("WORDS truncated: buffer full");
                    break;
                }
            } else {
                set_error(env,"WORDS: Null name in dictionary");
                break;
            }
        }
        send_to_channel(words_msg);
    } else {
        send_to_channel("Dictionary empty");
    }
    break;
    
case OP_LOAD: {} // vide 
break ; 
        case OP_PICK:
            pop(env, *a);
            int n = mpz_get_si(*a);
            if (stack->top >= n) push(env, stack->data[stack->top - n]);
            else set_error(env,"PICK: Stack underflow");
            break;
        case OP_ROLL:
            pop(env, *a);
            int zozo = mpz_get_si(*a);
            if (stack->top >= zozo) {
                mpz_t temp[zozo + 1];
                for (int i = 0; i <= zozo; i++) {
                    mpz_init(temp[i]);
                    pop(env, temp[i]);
                }
                for (int i = zozo - 1; i >= 0; i--) push(env, temp[i]);
                mpz_clear(temp[zozo]);
                for (int i = 0; i < zozo; i++) mpz_clear(temp[i]);
            } else set_error(env,"ROLL: Stack underflow");
            break;
case OP_PLUSSTORE:
    if (stack->top < 1) {
        set_error(env,"+!: Stack underflow");
        break;
    }
    pop(env, *a); // encoded_idx (ex. 268435456 pour ZOZO)
    if (stack->top < 0) {
        set_error(env,"+!: Stack underflow for value");
        push(env, *a);
        break;
    }
    pop(env, *b); // offset ou valeur (selon type)
    unsigned long encoded_idx = mpz_get_ui(*a);
    // char debug_msg[512];
    /* snprintf(debug_msg, sizeof(debug_msg), "+!: encoded_idx=%lu", encoded_idx);
    send_to_channel(debug_msg);
*/
    MemoryNode *node = memory_get(&env->memory_list, encoded_idx);
    if (!node) {
        snprintf(debug_msg, sizeof(debug_msg), "+!: Invalid memory index %lu", encoded_idx);
        set_error(env,debug_msg);
        push(env, *b);
        push(env, *a);
        break;
    }

    if (node->type == TYPE_VAR) {
        // Cas variable : *b est la valeur à ajouter
        mpz_add(node->value.number, node->value.number, *b);
        /* snprintf(debug_msg, sizeof(debug_msg), "+!: Added %s to %s, now %s", 
                 mpz_get_str(NULL, 10, *b), node->name, mpz_get_str(NULL, 10, node->value.number));
        send_to_channel(debug_msg);
        */ 
    } else if (node->type == TYPE_ARRAY) {
        // Cas tableau : vérifier s'il y a un offset sur la pile
        if (node->value.array.size == 0) {
        set_error(env,"tableau vide");
        push(env, *a);
        push(env, *b);
        break;
    }
        if (stack->top < 0) {
            // Pas d'offset : *b est la valeur, ajout à l'index 0
            if (node->value.array.size > 0) {
                mpz_add(node->value.array.data[0], node->value.array.data[0], *b);
                snprintf(debug_msg, sizeof(debug_msg), "+!: Added %s to %s[0], now %s", 
                         mpz_get_str(NULL, 10, *b), node->name, mpz_get_str(NULL, 10, node->value.array.data[0]));
                send_to_channel(debug_msg);
            } else {
                set_error(env,"+!: Array is empty");
                push(env, *b);
                push(env, *a);
            }
        } else {
            // Offset présent : dépiler la valeur, *b est l'offset
            pop(env, *result); // valeur à ajouter
            unsigned long offset = mpz_get_ui(*b); // *b est l'offset
            if (offset < node->value.array.size) {
                mpz_add(node->value.array.data[offset], node->value.array.data[offset], *result);
                /*snprintf(debug_msg, sizeof(debug_msg), "+!: Added %s to %s[%lu], now %s", 
                         mpz_get_str(NULL, 10, *result), node->name, offset, 
                         mpz_get_str(NULL, 10, node->value.array.data[offset]));
                send_to_channel(debug_msg);
                */ 
            } else {
                snprintf(debug_msg, sizeof(debug_msg), "+!: Offset %lu out of bounds (size=%lu)", 
                         offset, node->value.array.size);
                set_error(env,debug_msg);
                push(env, *result); // Remettre valeur
                push(env, *b);      // Remettre offset
                push(env, *a);      // Remettre encoded_idx
            }
        }
    } else {
        set_error(env,"+!: Not a variable or array");
        push(env, *b);
        push(env, *a);
    }
    break;
        case OP_DEPTH:
            mpz_set_si(*result, stack->top + 1);
            push(env, *result);
            break;
        case OP_TOP:
            if (stack->top >= 0) push(env, stack->data[stack->top]);
            else set_error(env,"TOP: Stack underflow");
            break;
        case OP_NIP:
            if (stack->top >= 1) {
                pop(env, *a);
                pop(env, *b);
                push(env, *a);
            } else set_error(env,"NIP: Stack underflow");
            break;

case OP_CREATE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name,env) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "CREATE: '%s' already defined", name);
            set_error(env,msg);
        } else {
            unsigned long index = memory_create(&env->memory_list, name, TYPE_ARRAY);
            if (index == 0) {
                set_error(env,"CREATE: Memory creation failed");
            } else if (env->dictionary.count < env->dictionary.capacity) {
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(name);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                MemoryNode *node = memory_get(&env->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)SAFE_MALLOC(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            } else {
                resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(name);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                MemoryNode *node = memory_get(&env->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)SAFE_MALLOC(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            }
        }
    } else {
        set_error(env,"CREATE: Invalid name");
    }
    break;
case OP_STRING:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name,env) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "STRING: '%s' already defined", name);
            set_error(env,msg);
        } else {
            unsigned long index = memory_create(&env->memory_list, name, TYPE_STRING);
            if (index == 0) {
                set_error(env,"STRING: Memory creation failed");
            } else if (env->dictionary.count < env->dictionary.capacity) {
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(name);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
            } else {
                resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(name);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
            }
        }
    } else {
        set_error(env,"STRING: Invalid name");
    }
    break;
case OP_QUOTE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *str = word->strings[instr.operand]; // Pas besoin de dupliquer ici, déjà alloué
        push_string(env,str); // Pousse sur string_stack
        mpz_set_si(*result, env->string_stack_top); // Index sur la pile principale
        push(env, *result);
    } else {
        set_error(env,"QUOTE: Invalid string index");
    }
    break;
    /* Ancienne version 
case OP_PRINT:
    pop(env, *a);
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= env->string_stack_top) {
        char *str = env->string_stack[mpz_get_si(*a)];
        if (str) {
            send_to_channel(str);
        } else {
            set_error(env,"PRINT: No string at index");
        }
    } else {
        set_error(env,"PRINT: Invalid string stack index");
    }
    break;
    */ 
    case OP_PRINT:
    pop(env, *a);
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= env->string_stack_top) {
        char *str = env->string_stack[mpz_get_si(*a)];
        if (str) {
            size_t len = strlen(str);
            if (env->buffer_pos + len < BUFFER_SIZE - 1) {
                strncpy(env->output_buffer + env->buffer_pos, str, len);
                env->buffer_pos += len;
                env->output_buffer[env->buffer_pos] = '\0';
            } else {
                set_error(env,"PRINT: Output buffer overflow");
            }
        } else {
            set_error(env,"PRINT: No string at index");
        }
    } else {
        set_error(env,"PRINT: Invalid string stack index");
    }
    break;
    case OP_NUM_TO_BIN:
        pop(env, *a);
        char *bin_str = mpz_get_str(NULL, 2, *a); // Base 2 = binaire
        send_to_channel(bin_str);
        free(bin_str); // Libérer la mémoire allouée par mpz_get_str
        break;
	case OP_PRIME_TEST:
        pop(env, *a);
        int is_prime = mpz_probab_prime_p(*a, 25); // 25 itérations de Miller-Rabin
        mpz_set_si(*result, is_prime != 0); // 1 si premier ou probablement premier, 0 sinon
        push(env, *result);
        break;
		case OP_CLEAR_STACK:
            stack->top = -1;  // Vide la pile en réinitialisant le sommet
            break;
        case OP_CLOCK:
            mpz_set_si(*result, (long int)time(NULL));
            push(env, *result);
            break;
case OP_IMAGE:
    if (stack->top < 0) {
        set_error(env,"IMAGE: Stack underflow");
        break;
    }
    pop(env, *a); // Index de la description dans string_stack
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= env->string_stack_top) {
        char *description = env->string_stack[mpz_get_si(*a)];
        if (description) {
            char *short_url = generate_image(description);
            if (short_url) {
                send_to_channel(short_url);
                free(short_url);
                // Nettoyer string_stack
                free(env->string_stack[mpz_get_si(*a)]);
                for (int i = mpz_get_si(*a); i < env->string_stack_top; i++) {
                    env->string_stack[i] = env->string_stack[i + 1];
                }
                env->string_stack[env->string_stack_top--] = NULL;
            } else {
                set_error(env,"IMAGE: Failed to generate or upload image");
            }
        } else {
            set_error(env,"IMAGE: No description string at index");
        }
    } else {
        set_error(env,"IMAGE: Invalid string stack index");
        push(env, *a);
    }
    break;
    case OP_TEMP_IMAGE:
    if (stack->top < 0) {
        set_error(env,"IMAGE: Stack underflow");
        break;
    }
    pop(env, *a); // Index de la description dans string_stack
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= env->string_stack_top) {
        char *description = env->string_stack[mpz_get_si(*a)];
        if (description) {
            char *short_url = generate_image_tiny(description);
            if (short_url) {
                send_to_channel(short_url);
                free(short_url);
                // Nettoyer string_stack
                free(env->string_stack[mpz_get_si(*a)]);
                for (int i = mpz_get_si(*a); i < env->string_stack_top; i++) {
                    env->string_stack[i] = env->string_stack[i + 1];
                }
                env->string_stack[env->string_stack_top--] = NULL;
            } else {
                set_error(env,"IMAGE: Failed to generate or upload image");
            }
        } else {
            set_error(env,"IMAGE: No description string at index");
        }
    } else {
        set_error(env,"IMAGE: Invalid string stack index");
        push(env, *a);
    }
    break;
    case OP_CLEAR_STRINGS:
     
        for (int i = 0; i <= env->string_stack_top; i++) {
            if (env->string_stack[i]) free(env->string_stack[i]);
        }
        env->string_stack_top = -1;
     
    break;
 
    case OP_DELAY:
    if (stack->top >= 0) {
        mpz_t delay_ms;
        mpz_init(delay_ms);
        pop(env, delay_ms);
        unsigned long ms = mpz_get_ui(delay_ms);
        struct timespec ts = {ms / 1000, (ms % 1000) * 1000000};
        nanosleep(&ts, NULL);
        mpz_clear(delay_ms);
    } else {
        set_error(env, "DELAY: Stack underflow");
    }
    break;
case OP_RECURSE:
/*
    if (word_index >= 0 && word_index < env->dictionary.count) {
        executeCompiledWord(&env->dictionary.words[word_index], stack, word_index, env);
    } else {
        set_error(env, "RECURSE: Invalid word index");
    }
    */
    break;
   case OP_CONSTANT:
     
    mpz_set_ui(*result, instr.operand);
    push(env, *result);
    break; 
    case OP_MICRO:

    gettimeofday(&tv, NULL);
    mpz_set_si(*result, (long int)(tv.tv_sec * 1000000 + tv.tv_usec)); // Microsecondes
    push(env, *result);
    break;
    case OP_MILLI:

    gettimeofday(&tv, NULL);
    mpz_set_si(*result, (long int)(tv.tv_sec * 1000 + tv.tv_usec / 1000)); // Millisecondes
    push(env, *result);
    break;
    case OP_APPEND:
    if (stack->top < 1) {
        set_error(env, "APPEND: Pile insuffisante");
        break;
    }
    pop(env, *b); // Indice du nom de fichier
    pop(env, *a); // Indice du texte
    if (!mpz_fits_slong_p(*a) || !mpz_fits_slong_p(*b)) {
        set_error(env, "APPEND: Indices de pile de chaînes invalides");
        push(env, *a);
        push(env, *b);
        break;
    }
    int text_idx = mpz_get_si(*a);
    int file_idx = mpz_get_si(*b);
    if (text_idx < 0 || text_idx > env->string_stack_top || file_idx < 0 || file_idx > env->string_stack_top) {
        set_error(env, "APPEND: Indices de pile de chaînes hors limites");
        push(env, *a);
        push(env, *b);
        break;
    }
    char *text = env->string_stack[text_idx];
    char *filename = env->string_stack[file_idx];
    if (!text || !filename) {
        set_error(env, "APPEND: Chaîne nulle dans la pile de chaînes");
        push(env, *a);
        push(env, *b);
        break;
    }
    FILE *file = fopen(filename, "a");
    if (!file) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "APPEND: Impossible d'ouvrir le fichier '%s'", filename);
        set_error(env, error_msg);
        push(env, *a);
        push(env, *b);
        break;
    }
    fprintf(file, "%s\n", text);
    fclose(file);
    break;
        default:
            set_error(env,"Unknown opcode");
            break;
    }
}
