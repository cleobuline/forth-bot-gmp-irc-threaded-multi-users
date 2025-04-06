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
#include "memory_forth.h"
#include <netdb.h> 
#include <curl/curl.h> 
#include "forth_bot.h"

void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index) {
    if (!currentenv || currentenv->error_flag) return;
    // printf( "Executing opcode %d at ip=%ld\n", instr.opcode, *ip);
    mpz_t *a = &mpz_pool[0], *b = &mpz_pool[1], *result = &mpz_pool[2];
    char temp_str[512];
unsigned long encoded_idx;
    unsigned long type;
    MemoryNode *node;
    switch (instr.opcode) {
 
case OP_PUSH:
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        if (mpz_set_str(*result, word->strings[instr.operand], 10) == 0) {
            push(stack, *result);
        } else {
            set_error("OP_PUSH: Invalid string number");
        }
    } else {
        mpz_set_ui(*result, instr.operand);
        push(stack, *result);
    }

    break;
        case OP_ADD:
            pop(stack, *b);
            pop(stack, *a);
            mpz_add(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_SUB:
            pop(stack, *b);
            pop(stack, *a);
            mpz_sub(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_MUL:
            pop(stack, *b);
            pop(stack, *a);
            mpz_mul(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_DIV:
            pop(stack, *b);
            pop(stack, *a);
            if (mpz_cmp_ui(*b, 0) == 0) {
                set_error("Division by zero");
                push(stack, *a);
                push(stack, *b);
            } else {
                mpz_div(*result, *a, *b);
                push(stack, *result);
            }
            break;
        case OP_MOD:
            pop(stack, *b);
            pop(stack, *a);
            if (mpz_cmp_ui(*b, 0) == 0) {
                set_error("Modulo by zero");
                push(stack, *a);
                push(stack, *b);
            } else {
                mpz_mod(*result, *a, *b);
                push(stack, *result);
            }
            break;
            
        case OP_DUP:
            if (stack->top >= 0) push(stack, stack->data[stack->top]);
            else set_error("DUP: Stack underflow");
            break;
        case OP_DROP:
            pop(stack, *a);
            break;
        case OP_SWAP:
            if (stack->top >= 1) {
                mpz_set(*a, stack->data[stack->top]);
                mpz_set(stack->data[stack->top], stack->data[stack->top - 1]);
                mpz_set(stack->data[stack->top - 1], *a);
            } else set_error("SWAP: Stack underflow");
            break;
        case OP_OVER:
            if (stack->top >= 1) push(stack, stack->data[stack->top - 1]);
            else set_error("OVER: Stack underflow");
            break;
        case OP_ROT:
            if (stack->top >= 2) {
                mpz_set(*a, stack->data[stack->top]);
                mpz_set(stack->data[stack->top], stack->data[stack->top - 2]);
                mpz_set(stack->data[stack->top - 2], stack->data[stack->top - 1]);
                mpz_set(stack->data[stack->top - 1], *a);
            } else set_error("ROT: Stack underflow");
            break;
        case OP_TO_R:
            pop(stack, *a);
            if (!currentenv->error_flag) {
                if (currentenv->return_stack.top < STACK_SIZE - 1) {
                    mpz_set(currentenv->return_stack.data[++currentenv->return_stack.top], *a);
                } else {
                    set_error(">R: Return stack overflow");
                    push(stack, *a);
                }
            }
            break;
        case OP_FROM_R:
            if (currentenv->return_stack.top >= 0) {
                mpz_set(*a, currentenv->return_stack.data[currentenv->return_stack.top--]);
                push(stack, *a);
            } else set_error("R>: Return stack underflow");
            break;
        case OP_R_FETCH:
            if (currentenv->return_stack.top >= 0) push(stack, currentenv->return_stack.data[currentenv->return_stack.top]);
            else set_error("R@: Return stack underflow");
            break;
        case OP_SEE:
       
    if (currentenv->compiling || word_index >= 0) { // Mode compilé ou dans une définition
        print_word_definition_irc(instr.operand, stack);
    } else { // Mode immédiat
        pop(stack, *a);
        if (!currentenv->error_flag && mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) < currentenv->dictionary.count) {
            print_word_definition_irc(mpz_get_si(*a), stack);
        } else if (!currentenv->error_flag) {
            set_error("SEE: Invalid word index");
        }
    }  
    break;
    case OP_2DROP:
    if (stack->top >= 1) {
        pop(stack, mpz_pool[0]); // Dépile le premier élément
        pop(stack, mpz_pool[0]); // Dépile le second élément
    } else {
        set_error("2DROP: Stack underflow");
    }
    break;
        case OP_EQ:
            pop(stack, *b);
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) == 0);
            push(stack, *result);
            break;
        case OP_LT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) < 0);
            push(stack, *result);
            break;
        case OP_GT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp(*a, *b) > 0);
            push(stack, *result);
            break;
case OP_AND:
    pop(stack, *b);
    pop(stack, *a);
    mpz_set_si(*result, (mpz_cmp_ui(*a, 0) != 0) && (mpz_cmp_ui(*b, 0) != 0));
    push(stack, *result);
    break;
case OP_OR:
    pop(stack, *b);
    pop(stack, *a);
    mpz_set_si(*result, (mpz_cmp_ui(*a, 0) != 0) || (mpz_cmp_ui(*b, 0) != 0));
    push(stack, *result);
    break;
        case OP_NOT:
            pop(stack, *a);
            mpz_set_si(*result, mpz_cmp_ui(*a, 0) == 0);
            push(stack, *result);
            break;
        case OP_XOR:
        pop(stack, *b);
        pop(stack, *a);
        int a_true = (mpz_cmp_ui(*a, 0) != 0);
        int b_true = (mpz_cmp_ui(*b, 0) != 0);
        mpz_set_si(*result, (a_true != b_true)); // Vrai si exactement une des deux est vraie
        push(stack, *result);
        break;
case OP_CALL:
    if (instr.operand >= 0 && instr.operand < currentenv->dictionary.count) {
        executeCompiledWord(&currentenv->dictionary.words[instr.operand], stack, instr.operand);
    } else {
        set_error("Invalid word index");
    }
    break;
        case OP_BRANCH:
            *ip = instr.operand - 1;
            break;
        case OP_BRANCH_FALSE:
            pop(stack, *a);
            if (mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_END:
            *ip = word->code_length;
            break;
        case OP_DOT:
            pop(stack, *a);
            mpz_get_str(temp_str, 10, *a);
            send_to_channel(temp_str);
            break;
 
        case OP_DOT_S:
            if (stack->top >= 0) {
                char stack_str[512] = "<";
                char num_str[128];
                snprintf(num_str, sizeof(num_str), "%ld", stack->top + 1);
                strcat(stack_str, num_str);
                strcat(stack_str, "> ");
                for (int i = 0; i <= stack->top; i++) {
                    mpz_get_str(num_str, 10, stack->data[i]);
                    strcat(stack_str, num_str);
                    if (i < stack->top) strcat(stack_str, " ");
                }
                send_to_channel(stack_str);
            } else send_to_channel("<0>");
            break;
case OP_EMIT:
    if (stack->top >= 0) {
        pop(stack, *a);
        char c = (char)mpz_get_si(*a);
        if (currentenv->buffer_pos < BUFFER_SIZE - 1) {
            currentenv->output_buffer[currentenv->buffer_pos++] = c;
            currentenv->output_buffer[currentenv->buffer_pos] = '\0';  // Terminer la chaîne
        } else {
            set_error("Output buffer overflow");
        }
    } else {
        set_error("EMIT: Stack underflow");
    }
    break;

case OP_CR:
    if (currentenv->buffer_pos < BUFFER_SIZE - 1) {
        currentenv->output_buffer[currentenv->buffer_pos++] = '\n';
        currentenv->output_buffer[currentenv->buffer_pos] = '\0';
    }
    if (currentenv->buffer_pos > 0) {
        send_to_channel(currentenv->output_buffer);  // Envoyer au canal IRC
        currentenv->buffer_pos = 0;  // Réinitialiser le buffer
        memset(currentenv->output_buffer, 0, BUFFER_SIZE);
    }
    break;
case OP_VARIABLE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "VARIABLE: '%s' already defined", name);
            set_error(msg);
        } else {
            unsigned long index = memory_create(&currentenv->memory_list, name, TYPE_VAR);
            if (index == 0) {
                set_error("VARIABLE: Memory creation failed");
            } else if (currentenv->dictionary.count < currentenv->dictionary.capacity) {
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            } else {
                resizeDynamicDictionary(&currentenv->dictionary);
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            }
        }
    } else {
        set_error("Invalid variable name");
    }
    break;
case OP_STORE:
    if (stack->top < 0) {
        set_error("STORE: Stack underflow for address");
        break;
    }
    char debug_msg[512];
    /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Starting, stack_top=%d", stack->top);
    send_to_channel(debug_msg);
 */
    pop(stack, *result); // encoded_idx
    encoded_idx = mpz_get_ui(*result);
   /*   snprintf(debug_msg, sizeof(debug_msg), "STORE: Popped encoded_idx=%lu, stack_top=%d", encoded_idx, stack->top);
    send_to_channel(debug_msg);
 */
    node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        snprintf(debug_msg, sizeof(debug_msg), "STORE: Invalid memory index for encoded_idx=%lu", encoded_idx);
        set_error(debug_msg);
        push(stack, *result);
        break;
    }
    type = node->type;
     /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Found node %s with type=%lu, stack_top=%d", node->name, type, stack->top);
    send_to_channel(debug_msg);
 */
    if (type == TYPE_VAR) {
        if (stack->top < 0) {
            set_error("STORE: Stack underflow for variable value");
            push(stack, *result);
            break;
        }
        pop(stack, *a);
        memory_store(&currentenv->memory_list, encoded_idx, a);
        /*  snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s = %s", node->name, mpz_get_str(NULL, 10, *a));
        send_to_channel(debug_msg);
        */
         
    } else if (type == TYPE_ARRAY) {
        if (stack->top < 1) {
            set_error("STORE: Stack underflow for array operation");
            push(stack, *result);
            break;
        }
        pop(stack, *a); // offset
        pop(stack, *b); // valeur
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
            set_error(debug_msg);
            push(stack, *b);
            push(stack, *a);
            push(stack, *result);
        }
    } else if (type == TYPE_STRING) {
        if (stack->top < 0) {
            set_error("STORE: Stack underflow for string value");
            push(stack, *result);
            break;
        }
        pop(stack, *a); // index dans string_stack
        int str_idx = mpz_get_si(*a);
        if (mpz_fits_slong_p(*a) && str_idx >= 0 && str_idx <= currentenv->string_stack_top) {
            char *str = currentenv->string_stack[str_idx];
            if (str) {
                memory_store(&currentenv->memory_list, encoded_idx, str);
                for (int i = str_idx; i < currentenv->string_stack_top; i++) {
                    currentenv->string_stack[i] = currentenv->string_stack[i + 1];
                }
                currentenv->string_stack[currentenv->string_stack_top--] = NULL;
                /* snprintf(debug_msg, sizeof(debug_msg), "STORE: Set %s = %s", node->name, str);
                send_to_channel(debug_msg);
                */ 
            } else {
                set_error("STORE: No string at stack index");
                push(stack, *a);
                push(stack, *result);
            }
        } else {
            set_error("STORE: Invalid string stack index");
            push(stack, *a);
            push(stack, *result);
        }
    } else {
        set_error("STORE: Unknown type");
        push(stack, *result);
    }
    break;
 
case OP_FETCH:
    if (stack->top < 0) {
        set_error("FETCH: Stack underflow for address");
        break;
    }
    pop(stack, *result); // encoded_idx (ex. ZOZO = 268435456)
    encoded_idx = mpz_get_ui(*result);
    // char debug_msg[512];
    /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Popped encoded_idx=%lu", encoded_idx);
    send_to_channel(debug_msg);
*/
    node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        set_error("FETCH: Invalid memory index");
        push(stack, *result);
        break;
    }
    type = node->type; // Type réel du nœud
    /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Found %s, type=%lu, stack_top=%d", node->name, type, stack->top);
    send_to_channel(debug_msg);
*/
    if (type == TYPE_VAR) {
        memory_fetch(&currentenv->memory_list, encoded_idx, result);
        /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Got variable %s = %s", node->name, mpz_get_str(NULL, 10, *result));
        send_to_channel(debug_msg);
        */
        push(stack, *result);
    } else if (type == TYPE_ARRAY) {
        if (stack->top < 0) {
            set_error("FETCH: Stack underflow for array offset");
            push(stack, *result);
            break;
        }
        pop(stack, *a); // offset (ex. 5)
        int offset = mpz_get_si(*a);
        /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Array offset=%d", offset);
        send_to_channel(debug_msg);
        */
        if (offset >= 0 && offset < node->value.array.size) {
            mpz_set(*result, node->value.array.data[offset]);
            /* snprintf(debug_msg, sizeof(debug_msg), "FETCH: Got %s[%d] = %s", node->name, offset, mpz_get_str(NULL, 10, *result));
            send_to_channel(debug_msg);
            */ 
            push(stack, *result);
        } else {
            snprintf(debug_msg, sizeof(debug_msg), "FETCH: Array index %d out of bounds (size=%lu)", offset, node->value.array.size);
            set_error(debug_msg);
            push(stack, *a);
            push(stack, *result);
        }
    } else if (type == TYPE_STRING) {
        char *str;
        memory_fetch(&currentenv->memory_list, encoded_idx, &str);
        if (str) {
            push_string(strdup(str));
            free(str);
            mpz_set_si(*result, currentenv->string_stack_top);
            push(stack, *result);
        } else {
            push_string(NULL);
            mpz_set_si(*result, currentenv->string_stack_top);
            push(stack, *result);
        }
    } else {
        set_error("FETCH: Unknown type");
        push(stack, *result);
    }
    break;
    
 
 
 case OP_ALLOT:
    if (stack->top < 1) { // Vérifie 2 éléments
        set_error("ALLOT: Stack underflow");
        break;
    }
    pop(stack, *a); // Taille
    pop(stack, *result); // encoded_idx
    encoded_idx = mpz_get_ui(*result);
    node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        set_error("ALLOT: Invalid memory index");
        push(stack, *result);
        push(stack, *a);
        break;
    }
    if (node->type != TYPE_ARRAY) {
        set_error("ALLOT: Must be an array");
        push(stack, *result);
        push(stack, *a);
        break;
    }
    int size = mpz_get_si(*a);
    if (size < 0) { // Accepte 0, mais négatif interdit
        set_error("ALLOT: Size must be non-negative");
        push(stack, *result);
        push(stack, *a);
        break;
    }
    unsigned long new_size = node->value.array.size + size;
    mpz_t *new_array = realloc(node->value.array.data, new_size * sizeof(mpz_t));
    if (!new_array) {
        set_error("ALLOT: Memory allocation failed");
        push(stack, *result);
        push(stack, *a);
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
        set_error("DO: Stack underflow");
        break;
    }
    pop(stack, *b); // index initial
    pop(stack, *a); // limite
    if (currentenv->return_stack.top + 3 >= STACK_SIZE) {
        set_error("DO: Return stack overflow");
        push(stack, *a);
        push(stack, *b);
        break;
    }
    currentenv->return_stack.top++;
    mpz_set(currentenv->return_stack.data[currentenv->return_stack.top], *a); // limite
    currentenv->return_stack.top++;
    mpz_set(currentenv->return_stack.data[currentenv->return_stack.top], *b); // index
    currentenv->return_stack.top++;
    mpz_set_si(currentenv->return_stack.data[currentenv->return_stack.top], *ip + 1); // adresse de retour
    break;

case OP_LOOP:
    if (currentenv->return_stack.top < 2) {
        set_error("LOOP: Return stack underflow");
        break;
    }
    mpz_add_ui(*result, currentenv->return_stack.data[currentenv->return_stack.top - 1], 1); // index + 1
    if (mpz_cmp(*result, currentenv->return_stack.data[currentenv->return_stack.top - 2]) < 0) { // index < limit
        mpz_set(currentenv->return_stack.data[currentenv->return_stack.top - 1], *result);
        *ip = instr.operand - 1; // Retour à l'instruction après DO
    } else {
        currentenv->return_stack.top -= 3; // Dépiler limit, index, addr
    }
    break;

case OP_I:
    if (currentenv->return_stack.top < 1) {
        set_error("I: Return stack underflow");
        break;
    }
    push(stack, currentenv->return_stack.data[currentenv->return_stack.top - 1]); // Pousse l'index actuel
    break;

case OP_J:
    if (currentenv->return_stack.top >= 1) {
        if (currentenv->return_stack.top >= 4) {
            // Boucle imbriquée : lit l’indice de la boucle externe
            push(stack, currentenv->return_stack.data[currentenv->return_stack.top - 4]);
        } else {
            // Boucle externe seule : lit l’indice courant
            push(stack, currentenv->return_stack.data[currentenv->return_stack.top - 1]);
        }
    } else {
        set_error("J: No outer loop");
    }
    break;
        case OP_UNLOOP:
    if (currentenv->return_stack.top >= 2) {
        currentenv->return_stack.top -= 3;  // Dépile limit, index, addr
    } else {
        set_error("UNLOOP without DO");
    }
    break;
case OP_PLUS_LOOP:
    if (stack->top < 0 || currentenv->return_stack.top < 2) {
        set_error("+LOOP: Stack or return stack underflow");
        break;
    }
    pop(stack, *a); // Pas
    mpz_t *index = &currentenv->return_stack.data[currentenv->return_stack.top - 1]; // Prendre l'adresse
    mpz_t *limit = &currentenv->return_stack.data[currentenv->return_stack.top - 2]; // Prendre l'adresse
    mpz_add(*index, *index, *a); // index += pas
    if (mpz_sgn(*a) >= 0) {
        if (mpz_cmp(*index, *limit) < 0) {
            *ip = instr.operand - 1; // Retour à DO
        } else {
            currentenv->return_stack.top -= 3;
        }
    } else {
        if (mpz_cmp(*index, *limit) > 0) {
            *ip = instr.operand - 1; // Retour à DO
        } else {
            currentenv->return_stack.top -= 3;
        }
    }
    break;
        case OP_SQRT:
            pop(stack, *a);
            if (mpz_cmp_ui(*a, 0) < 0) {
                set_error("Square root of negative number");
                push(stack, *a);
            } else {
                mpz_sqrt(*result, *a);
                push(stack, *result);
            }
        break ; 
        /* 
case OP_DOT_QUOTE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        send_to_channel(word->strings[instr.operand]);
    } else {
        set_error("Invalid string index for .\"");
    }
    break;
    */ 
    case OP_DOT_QUOTE:
    if (instr.operand < word->string_count && word->strings[instr.operand]) {
        size_t len = strlen(word->strings[instr.operand]);
        if (currentenv->buffer_pos + len < BUFFER_SIZE - 1) {
            strncpy(currentenv->output_buffer + currentenv->buffer_pos, word->strings[instr.operand], len);
            currentenv->buffer_pos += len;
            currentenv->output_buffer[currentenv->buffer_pos] = '\0';
        } else {
            set_error("DOT_QUOTE: Output buffer overflow");
        }
    } else {
        set_error("DOT_QUOTE: Invalid string index");
    }
    break;
 
        case OP_CASE:
            if (currentenv->control_stack_top < CONTROL_STACK_SIZE) {
                currentenv->control_stack[currentenv->control_stack_top].type = CT_CASE;
                currentenv->control_stack[currentenv->control_stack_top].addr = *ip;
                currentenv->control_stack_top++;
            } else set_error("Control stack overflow");
            break;
        case OP_OF:
            pop(stack, *a);
            pop(stack, *b);
            if (mpz_cmp(*a, *b) != 0) {
                *ip = instr.operand - 1; // Sauter à ENDOF
                push(stack, *b); // Remettre la valeur de test
            } else {
                push(stack, *b); // Remettre la valeur de test
            }
            break;
        case OP_ENDOF:
            *ip = instr.operand - 1; // Sauter à ENDCASE ou prochain ENDOF
            break;
        case OP_ENDCASE:
            pop(stack, *a); // Dépiler la valeur de test
            if (currentenv->control_stack_top > 0 && currentenv->control_stack[currentenv->control_stack_top - 1].type == CT_CASE) {
                currentenv->control_stack_top--;
            } else set_error("ENDCASE without CASE");
            break;
        case OP_EXIT:
            *ip = word->code_length;
            break;
case OP_BEGIN:
    if (currentenv->control_stack_top < CONTROL_STACK_SIZE) {
        currentenv->control_stack[currentenv->control_stack_top++] = (ControlEntry){CT_BEGIN, *ip};
    } else set_error("Control stack overflow");
    break;
case OP_WHILE:
    pop(stack, *a);
    if (!currentenv->error_flag && mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1; // Sauter si faux
    break;
case OP_REPEAT:
    *ip = instr.operand - 1; // Toujours sauter à BEGIN, pas besoin de vérifier control_stack ici
    break;
        case OP_UNTIL:
            pop(stack, *a);
            if (mpz_cmp_ui(*a, 0) == 0) *ip = instr.operand - 1;
            break;
        case OP_AGAIN:
            *ip = instr.operand - 1;
            break;
        case OP_BIT_AND:
            pop(stack, *b);
            pop(stack, *a);
            mpz_and(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_BIT_OR:
            pop(stack, *b);
            pop(stack, *a);
            mpz_ior(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_BIT_XOR:
            pop(stack, *b);
            pop(stack, *a);
            mpz_xor(*result, *a, *b);
            push(stack, *result);
            break;
        case OP_BIT_NOT:
            pop(stack, *a);
            mpz_com(*result, *a);
            push(stack, *result);
            break;
        case OP_LSHIFT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_mul_2exp(*result, *a, mpz_get_ui(*b));
            push(stack, *result);
            break;
        case OP_RSHIFT:
            pop(stack, *b);
            pop(stack, *a);
            mpz_fdiv_q_2exp(*result, *a, mpz_get_ui(*b));
            push(stack, *result);
            break;
case OP_FORGET:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *word_to_forget = word->strings[instr.operand];
        int forget_idx = findCompiledWordIndex(word_to_forget);
        if (forget_idx >= 0) {
            for (int i = forget_idx; i < currentenv->dictionary.count; i++) {
                CompiledWord *dict_word = &currentenv->dictionary.words[i];
                if (dict_word->code_length == 1 && dict_word->code[0].opcode == OP_PUSH) {
                    unsigned long encoded_idx = dict_word->code[0].operand;
                    unsigned long type = memory_get_type(encoded_idx);
                    if (type == TYPE_VAR || type == TYPE_STRING || type == TYPE_ARRAY) {
                        memory_free(&currentenv->memory_list, dict_word->name);
                    }
                }
                if (dict_word->name) {
                    free(dict_word->name);
                    dict_word->name = NULL;
                }
                for (int j = 0; j < dict_word->string_count; j++) {
                    if (dict_word->strings[j]) {
                        free(dict_word->strings[j]);
                        dict_word->strings[j] = NULL;
                    }
                }
                dict_word->code_length = 0;
                dict_word->string_count = 0;
            }
            long int old_dict_count = currentenv->dictionary.count;
            currentenv->dictionary.count = forget_idx;
            char msg[512];
            snprintf(msg, sizeof(msg), "Forgot everything from '%s' at index %d (dict was %ld, now %ld; mem count now %lu)", 
                     word_to_forget, forget_idx, old_dict_count, currentenv->dictionary.count, currentenv->memory_list.count);
            send_to_channel(msg);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "FORGET: Unknown word: %s", word_to_forget);
            set_error(msg);
        }
    } else {
        set_error("FORGET: Invalid word name");
    }
    break;
case OP_WORDS:
    if (currentenv->dictionary.count > 0) {
        char words_msg[2048] = "";
        size_t remaining = sizeof(words_msg) - 1;
        for (int i = 0; i < currentenv->dictionary.count && remaining > 1; i++) {
            if (currentenv->dictionary.words[i].name) {
                size_t name_len = strlen(currentenv->dictionary.words[i].name);
                if (name_len + 1 < remaining) {
                    strncat(words_msg, currentenv->dictionary.words[i].name, remaining);
                    strncat(words_msg, " ", remaining - name_len);
                    remaining -= (name_len + 1);
                } else {
                    send_to_channel("WORDS truncated: buffer full");
                    break;
                }
            } else {
                set_error("WORDS: Null name in dictionary");
                break;
            }
        }
        send_to_channel(words_msg);
    } else {
        send_to_channel("Dictionary empty");
    }
    break;
    
case OP_LOAD: {
    char *filename = NULL;
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        filename = strdup(word->strings[instr.operand]);
    } else {
        set_error("LOAD: No filename provided");
        break;
    }

    FILE *file = fopen(filename, "r");
    if (file) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), file)) {
            buffer[strcspn(buffer, "\n")] = '\0';
            interpret(buffer, stack); // Utiliser la pile passée (stack, pas forcément main_stack)
        }
        fclose(file);
    } else {
        snprintf(temp_str, sizeof(temp_str), "LOAD: Cannot open file '%s'", filename);
        set_error(temp_str);
    }
    free(filename);
    break;
}
 
        case OP_PICK:
            pop(stack, *a);
            int n = mpz_get_si(*a);
            if (stack->top >= n) push(stack, stack->data[stack->top - n]);
            else set_error("PICK: Stack underflow");
            break;
        case OP_ROLL:
            pop(stack, *a);
            int zozo = mpz_get_si(*a);
            if (stack->top >= zozo) {
                mpz_t temp[zozo + 1];
                for (int i = 0; i <= zozo; i++) {
                    mpz_init(temp[i]);
                    pop(stack, temp[i]);
                }
                for (int i = zozo - 1; i >= 0; i--) push(stack, temp[i]);
                mpz_clear(temp[zozo]);
                for (int i = 0; i < zozo; i++) mpz_clear(temp[i]);
            } else set_error("ROLL: Stack underflow");
            break;
case OP_PLUSSTORE:
    if (stack->top < 1) {
        set_error("+!: Stack underflow");
        break;
    }
    pop(stack, *a); // encoded_idx (ex. 268435456 pour ZOZO)
    if (stack->top < 0) {
        set_error("+!: Stack underflow for value");
        push(stack, *a);
        break;
    }
    pop(stack, *b); // offset ou valeur (selon type)
    unsigned long encoded_idx = mpz_get_ui(*a);
    // char debug_msg[512];
    /* snprintf(debug_msg, sizeof(debug_msg), "+!: encoded_idx=%lu", encoded_idx);
    send_to_channel(debug_msg);
*/
    MemoryNode *node = memory_get(&currentenv->memory_list, encoded_idx);
    if (!node) {
        snprintf(debug_msg, sizeof(debug_msg), "+!: Invalid memory index %lu", encoded_idx);
        set_error(debug_msg);
        push(stack, *b);
        push(stack, *a);
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
        set_error("tableau vide");
        push(stack, *a);
        push(stack, *b);
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
                set_error("+!: Array is empty");
                push(stack, *b);
                push(stack, *a);
            }
        } else {
            // Offset présent : dépiler la valeur, *b est l'offset
            pop(stack, *result); // valeur à ajouter
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
                set_error(debug_msg);
                push(stack, *result); // Remettre valeur
                push(stack, *b);      // Remettre offset
                push(stack, *a);      // Remettre encoded_idx
            }
        }
    } else {
        set_error("+!: Not a variable or array");
        push(stack, *b);
        push(stack, *a);
    }
    break;
        case OP_DEPTH:
            mpz_set_si(*result, stack->top + 1);
            push(stack, *result);
            break;
        case OP_TOP:
            if (stack->top >= 0) push(stack, stack->data[stack->top]);
            else set_error("TOP: Stack underflow");
            break;
        case OP_NIP:
            if (stack->top >= 1) {
                pop(stack, *a);
                pop(stack, *b);
                push(stack, *a);
            } else set_error("NIP: Stack underflow");
            break;

case OP_CREATE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "CREATE: '%s' already defined", name);
            set_error(msg);
        } else {
            unsigned long index = memory_create(&currentenv->memory_list, name, TYPE_ARRAY);
            if (index == 0) {
                set_error("CREATE: Memory creation failed");
            } else if (currentenv->dictionary.count < currentenv->dictionary.capacity) {
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
                MemoryNode *node = memory_get(&currentenv->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)malloc(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            } else {
                resizeDynamicDictionary(&currentenv->dictionary);
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
                MemoryNode *node = memory_get(&currentenv->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)malloc(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            }
        }
    } else {
        set_error("CREATE: Invalid name");
    }
    break;
case OP_STRING:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *name = word->strings[instr.operand];
        if (findCompiledWordIndex(name) >= 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "STRING: '%s' already defined", name);
            set_error(msg);
        } else {
            unsigned long index = memory_create(&currentenv->memory_list, name, TYPE_STRING);
            if (index == 0) {
                set_error("STRING: Memory creation failed");
            } else if (currentenv->dictionary.count < currentenv->dictionary.capacity) {
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            } else {
                resizeDynamicDictionary(&currentenv->dictionary);
                int dict_idx = currentenv->dictionary.count++;
                currentenv->dictionary.words[dict_idx].name = strdup(name);
                currentenv->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                currentenv->dictionary.words[dict_idx].code[0].operand = index;
                currentenv->dictionary.words[dict_idx].code_length = 1;
                currentenv->dictionary.words[dict_idx].string_count = 0;
            }
        }
    } else {
        set_error("STRING: Invalid name");
    }
    break;
case OP_QUOTE:
    if (instr.operand >= 0 && instr.operand < word->string_count && word->strings[instr.operand]) {
        char *str = word->strings[instr.operand]; // Pas besoin de dupliquer ici, déjà alloué
        push_string(str); // Pousse sur string_stack
        mpz_set_si(*result, currentenv->string_stack_top); // Index sur la pile principale
        push(stack, *result);
    } else {
        set_error("QUOTE: Invalid string index");
    }
    break;
    /* Ancienne version 
case OP_PRINT:
    pop(stack, *a);
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= currentenv->string_stack_top) {
        char *str = currentenv->string_stack[mpz_get_si(*a)];
        if (str) {
            send_to_channel(str);
        } else {
            set_error("PRINT: No string at index");
        }
    } else {
        set_error("PRINT: Invalid string stack index");
    }
    break;
    */ 
    case OP_PRINT:
    pop(stack, *a);
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= currentenv->string_stack_top) {
        char *str = currentenv->string_stack[mpz_get_si(*a)];
        if (str) {
            size_t len = strlen(str);
            if (currentenv->buffer_pos + len < BUFFER_SIZE - 1) {
                strncpy(currentenv->output_buffer + currentenv->buffer_pos, str, len);
                currentenv->buffer_pos += len;
                currentenv->output_buffer[currentenv->buffer_pos] = '\0';
            } else {
                set_error("PRINT: Output buffer overflow");
            }
        } else {
            set_error("PRINT: No string at index");
        }
    } else {
        set_error("PRINT: Invalid string stack index");
    }
    break;
    case OP_NUM_TO_BIN:
        pop(stack, *a);
        char *bin_str = mpz_get_str(NULL, 2, *a); // Base 2 = binaire
        send_to_channel(bin_str);
        free(bin_str); // Libérer la mémoire allouée par mpz_get_str
        break;
	case OP_PRIME_TEST:
        pop(stack, *a);
        int is_prime = mpz_probab_prime_p(*a, 25); // 25 itérations de Miller-Rabin
        mpz_set_si(*result, is_prime != 0); // 1 si premier ou probablement premier, 0 sinon
        push(stack, *result);
        break;
		case OP_CLEAR_STACK:
            stack->top = -1;  // Vide la pile en réinitialisant le sommet
            break;
        case OP_CLOCK:
            mpz_set_si(*result, (long int)time(NULL));
            push(stack, *result);
            break;
case OP_IMAGE:
    if (stack->top < 0) {
        set_error("IMAGE: Stack underflow");
        break;
    }
    pop(stack, *a); // Index de la description dans string_stack
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= currentenv->string_stack_top) {
        char *description = currentenv->string_stack[mpz_get_si(*a)];
        if (description) {
            char *short_url = generate_image(description);
            if (short_url) {
                send_to_channel(short_url);
                free(short_url);
                // Nettoyer string_stack
                free(currentenv->string_stack[mpz_get_si(*a)]);
                for (int i = mpz_get_si(*a); i < currentenv->string_stack_top; i++) {
                    currentenv->string_stack[i] = currentenv->string_stack[i + 1];
                }
                currentenv->string_stack[currentenv->string_stack_top--] = NULL;
            } else {
                set_error("IMAGE: Failed to generate or upload image");
            }
        } else {
            set_error("IMAGE: No description string at index");
        }
    } else {
        set_error("IMAGE: Invalid string stack index");
        push(stack, *a);
    }
    break;
    case OP_TEMP_IMAGE:
    if (stack->top < 0) {
        set_error("IMAGE: Stack underflow");
        break;
    }
    pop(stack, *a); // Index de la description dans string_stack
    if (mpz_fits_slong_p(*a) && mpz_get_si(*a) >= 0 && mpz_get_si(*a) <= currentenv->string_stack_top) {
        char *description = currentenv->string_stack[mpz_get_si(*a)];
        if (description) {
            char *short_url = generate_image_tiny(description);
            if (short_url) {
                send_to_channel(short_url);
                free(short_url);
                // Nettoyer string_stack
                free(currentenv->string_stack[mpz_get_si(*a)]);
                for (int i = mpz_get_si(*a); i < currentenv->string_stack_top; i++) {
                    currentenv->string_stack[i] = currentenv->string_stack[i + 1];
                }
                currentenv->string_stack[currentenv->string_stack_top--] = NULL;
            } else {
                set_error("IMAGE: Failed to generate or upload image");
            }
        } else {
            set_error("IMAGE: No description string at index");
        }
    } else {
        set_error("IMAGE: Invalid string stack index");
        push(stack, *a);
    }
    break;
        	case OP_CLEAR_STRINGS:
    if (strcmp(word->name, "CLEAR-STRINGS") == 0) {
        for (int i = 0; i <= currentenv->string_stack_top; i++) {
            if (currentenv->string_stack[i]) free(currentenv->string_stack[i]);
        }
        currentenv->string_stack_top = -1;
    } else {
        stack->top = -1;
    }
    break;
case OP_DELAY:
    if (stack->top >= 0) {
        mpz_t delay_ms;
        mpz_init(delay_ms);
        pop(stack, delay_ms);
        volatile unsigned long ms = mpz_get_ui(delay_ms); // Volatile pour éviter l’optimisation
        usleep(ms * 1000);
        mpz_clear(delay_ms);
    } else {
        set_error("DELAY: Stack underflow");
    }
    break;
   case OP_RECURSE:
   break;
    
   case OP_CONSTANT:
     
    mpz_set_ui(*result, instr.operand);
    push(stack, *result);
    break; 
    
        default:
            set_error("Unknown opcode");
            break;
    }
}
