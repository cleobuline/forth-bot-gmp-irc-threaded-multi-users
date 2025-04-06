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

void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index) {
    long int ip = 0;
    while (ip < word->code_length && !currentenv->error_flag) {
        executeInstruction(word->code[ip], stack, &ip, word, word_index);
        ip++;
    }
}
void compileToken(char *token, char **input_rest, Env *env) {
    Instruction instr = {0};
    if (!env || env->compile_error) return;

    // Mots immédiats (exécutés même en mode compilation)
    int idx = findCompiledWordIndex(token);
    if (idx >= 0 && env->dictionary.words[idx].immediate) {
        if (strcmp(token, "STRING") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("STRING requires a name");
                env->compile_error = 1;
                return;
            }
            if (findCompiledWordIndex(next_token) >= 0) {
                char msg[512];
                snprintf(msg, sizeof(msg), "STRING: '%s' already defined", next_token);
                set_error(msg);
                env->compile_error = 1;
                return;
            }
            unsigned long index = memory_create(&env->memory_list, next_token, TYPE_STRING);
            if (index == 0) {
                set_error("STRING: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup(next_token);
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = index;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            env->dictionary.words[dict_idx].immediate = 0;
            if (env->compiling) {
                instr.opcode = OP_STRING;
                instr.operand = env->currentWord.string_count;
                env->currentWord.strings[env->currentWord.string_count++] = strdup(next_token);
                env->currentWord.code[env->currentWord.code_length++] = instr;
            }
        } else if (strcmp(token, "FORGET") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("FORGET requires a word name");
                env->compile_error = 1;
                return;
            }
            CompiledWord temp_word = {0};
            temp_word.strings[0] = strdup(next_token);
            temp_word.string_count = 1;
            instr.opcode = OP_FORGET;
            instr.operand = 0;
            executeInstruction(instr, &env->main_stack, NULL, &temp_word, -1);
            free(temp_word.strings[0]);
            return;
        }
        return; // Sortir après un mot immédiat
    }

    // Début d’une définition
    if (strcmp(token, ":") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            set_error("No name for definition");
            env->compile_error = 1;
            return;
        }
        env->compiling = 1;
        int existing_idx = findCompiledWordIndex(next_token);
        if (existing_idx >= 0) {
            env->current_word_index = existing_idx;
            if (env->dictionary.words[existing_idx].name) free(env->dictionary.words[existing_idx].name);
            for (int j = 0; j < env->dictionary.words[existing_idx].string_count; j++) {
                if (env->dictionary.words[existing_idx].strings[j]) free(env->dictionary.words[existing_idx].strings[j]);
            }
            env->dictionary.words[existing_idx].name = NULL;
            env->dictionary.words[existing_idx].code_length = 0;
            env->dictionary.words[existing_idx].string_count = 0;
        } else {
            env->current_word_index = env->dictionary.count;
            if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
            env->dictionary.count++;
        }
        env->currentWord.name = strdup(next_token);
        env->currentWord.code_length = 0;
        env->currentWord.string_count = 0;
        env->control_stack_top = 0;
        return;
    }

    // Fin d’une définition
    if (strcmp(token, ";") == 0) {
        if (!env->compiling) {
            set_error("Extra ;");
            env->compile_error = 1;
            return;
        }
        if (env->control_stack_top > 0) {
            set_error("Unmatched control structures");
            env->compile_error = 1;
            env->control_stack_top = 0;
            env->compiling = 0;
            return;
        }
        instr.opcode = OP_END;
        env->currentWord.code[env->currentWord.code_length++] = instr;
        if (env->current_word_index >= 0 && env->current_word_index < env->dictionary.capacity) {
            env->dictionary.words[env->current_word_index] = env->currentWord;
            if (env->current_word_index == env->dictionary.count) env->dictionary.count++;
        } else {
            set_error("Dictionary index out of bounds");
            env->compile_error = 1;
        }
        env->compiling = 0;
        env->compile_error = 0;
        env->control_stack_top = 0;
        env->currentWord.name = NULL;
        env->currentWord.code_length = 0;
        env->currentWord.string_count = 0;
        return;
    }

    // Récursion dans une définition
    if (strcmp(token, "RECURSE") == 0) {
        if (!env->compiling) {
            set_error("RECURSE outside definition");
            env->compile_error = 1;
            return;
        }
        instr.opcode = OP_CALL;
        instr.operand = env->current_word_index;
        env->currentWord.code[env->currentWord.code_length++] = instr;
        return;
    }

    // Affichage d’une définition avec SEE
    if (strcmp(token, "SEE") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            send_to_channel("SEE requires a word name");
            env->compile_error = 1;
            return;
        }
        int index = findCompiledWordIndex(next_token);
        if (index >= 0) {
            print_word_definition_irc(index, &env->main_stack);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "SEE: Unknown word: %s", next_token);
            send_to_channel(msg);
            env->compile_error = 1;
        }
        return;
    }

    // Mode compilation : construire le mot
    if (env->compiling) {
        if (strcmp(token, "DUP") == 0) instr.opcode = OP_DUP;
        else if (strcmp(token, "DROP") == 0) instr.opcode = OP_DROP;
        else if (strcmp(token, "SWAP") == 0) instr.opcode = OP_SWAP;
        else if (strcmp(token, "OVER") == 0) instr.opcode = OP_OVER;
        else if (strcmp(token, "ROT") == 0) instr.opcode = OP_ROT;
        else if (strcmp(token, "+") == 0) instr.opcode = OP_ADD;
        else if (strcmp(token, "-") == 0) instr.opcode = OP_SUB;
        else if (strcmp(token, "*") == 0) instr.opcode = OP_MUL;
        else if (strcmp(token, "/") == 0) instr.opcode = OP_DIV;
        else if (strcmp(token, "MOD") == 0) instr.opcode = OP_MOD;
        else if (strcmp(token, "=") == 0) instr.opcode = OP_EQ;
        else if (strcmp(token, "<") == 0) instr.opcode = OP_LT;
        else if (strcmp(token, ">") == 0) instr.opcode = OP_GT;
        else if (strcmp(token, "AND") == 0) instr.opcode = OP_AND;
        else if (strcmp(token, "OR") == 0) instr.opcode = OP_OR;
        else if (strcmp(token, "NOT") == 0) instr.opcode = OP_NOT;
        else if (strcmp(token, "XOR") == 0) instr.opcode = OP_XOR;
        else if (strcmp(token, ".") == 0) instr.opcode = OP_DOT;
        else if (strcmp(token, ".S") == 0) instr.opcode = OP_DOT_S;
        else if (strcmp(token, "CR") == 0) instr.opcode = OP_CR;
        else if (strcmp(token, "EMIT") == 0) instr.opcode = OP_EMIT;
        else if (strcmp(token, "@") == 0) instr.opcode = OP_FETCH;
        else if (strcmp(token, "!") == 0) instr.opcode = OP_STORE;
        else if (strcmp(token, ">R") == 0) instr.opcode = OP_TO_R;
        else if (strcmp(token, "R>") == 0) instr.opcode = OP_FROM_R;
        else if (strcmp(token, "R@") == 0) instr.opcode = OP_R_FETCH;
        else if (strcmp(token, "I") == 0) instr.opcode = OP_I;
        else if (strcmp(token, "J") == 0) instr.opcode = OP_J;
        else if (strcmp(token, "DO") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_DO;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_DO, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "LOOP") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_DO) {
                set_error("LOOP without DO");
                env->compile_error = 1;
                return;
            }
            ControlEntry do_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_LOOP;
            instr.operand = do_entry.addr + 1;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "+LOOP") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_DO) {
                set_error("+LOOP without DO");
                env->compile_error = 1;
                return;
            }
            ControlEntry do_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_PLUS_LOOP;
            instr.operand = do_entry.addr + 1;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "PICK") == 0) instr.opcode = OP_PICK;
        else if (strcmp(token, "EXIT") == 0) instr.opcode = OP_EXIT;
        else if (strcmp(token, "CLOCK") == 0) instr.opcode = OP_CLOCK;
        else if (strcmp(token, "CLEAR-STACK") == 0) instr.opcode = OP_CLEAR_STACK;
        else if (strcmp(token, "WORDS") == 0) instr.opcode = OP_WORDS;
        else if (strcmp(token, "NUM-TO-BIN") == 0) instr.opcode = OP_NUM_TO_BIN;
        else if (strcmp(token, "PRIME?") == 0) instr.opcode = OP_PRIME_TEST;
        else if (strcmp(token, "&") == 0) instr.opcode = OP_BIT_AND;
        else if (strcmp(token, "|") == 0) instr.opcode = OP_BIT_OR;
        else if (strcmp(token, "^") == 0) instr.opcode = OP_BIT_XOR;
        else if (strcmp(token, "~") == 0) instr.opcode = OP_BIT_NOT;
        else if (strcmp(token, "<<") == 0) instr.opcode = OP_LSHIFT;
        else if (strcmp(token, ">>") == 0) instr.opcode = OP_RSHIFT;
        else if (strcmp(token, ".\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error(".\" expects a string ending with \"");
                env->compile_error = 1;
                return;
            }
            long int len = end - start;
            char *str = malloc(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            instr.opcode = OP_DOT_QUOTE;
            instr.operand = env->currentWord.string_count;
            env->currentWord.strings[env->currentWord.string_count++] = str;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            return;
        }
        else if (strcmp(token, "VARIABLE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("VARIABLE requires a name");
                env->compile_error = 1;
                return;
            }
            int existing_idx = findCompiledWordIndex(next_token);
            unsigned long encoded_index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (encoded_index == 0) {
                set_error("VARIABLE: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                word->name = strdup(next_token);
                word->code[0].opcode = OP_PUSH;
                word->code[0].operand = encoded_index;
                word->code_length = 1;
                word->string_count = 0;
                word->immediate = 0;
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = encoded_index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
            }
            return;
        }
        else if (strcmp(token, "CONSTANT") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("CONSTANT requires a name");
                env->compile_error = 1;
                return;
            }
            if (env->main_stack.top < 0) {
                set_error("CONSTANT: Stack underflow");
                env->compile_error = 1;
                return;
            }
            mpz_t value;
            pop(&env->main_stack, value);
            int existing_idx = findCompiledWordIndex(next_token);
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                word->name = strdup(next_token);
                word->code[0].opcode = OP_CONSTANT;
                word->code[0].operand = mpz_get_ui(value);
                word->code_length = 1;
                word->string_count = 0;
                word->immediate = 0;
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code[0].opcode = OP_CONSTANT;
                env->dictionary.words[dict_idx].code[0].operand = mpz_get_ui(value);
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
            }
            // En mode compilation, ajouter l’instruction
            if (env->compiling) {
                instr.opcode = OP_CONSTANT;
                instr.operand = mpz_get_ui(value);
                env->currentWord.code[env->currentWord.code_length++] = instr;
            }
            return;
        }
        else if (strcmp(token, "\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error("Missing closing quote for \"");
                env->compile_error = 1;
                return;
            }
            long int len = end - start;
            char *str = malloc(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            instr.opcode = OP_QUOTE;
            instr.operand = env->currentWord.string_count;
            env->currentWord.strings[env->currentWord.string_count++] = str;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            return;
        }
        else if (strcmp(token, "IF") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_BRANCH_FALSE;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_IF, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ELSE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_IF) {
                set_error("ELSE without IF");
                env->compile_error = 1;
                return;
            }
            ControlEntry if_entry = env->control_stack[env->control_stack_top - 1];
            env->currentWord.code[if_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_BRANCH;
            instr.operand = 0;
            env->control_stack[env->control_stack_top - 1].type = CT_ELSE;
            env->control_stack[env->control_stack_top - 1].addr = env->currentWord.code_length;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "THEN") == 0) {
            if (env->control_stack_top <= 0) {
                set_error("THEN without IF or ELSE");
                env->compile_error = 1;
                return;
            }
            ControlEntry entry = env->control_stack[--env->control_stack_top];
            if (entry.type == CT_IF || entry.type == CT_ELSE) {
                env->currentWord.code[entry.addr].operand = env->currentWord.code_length;
            } else {
                set_error("Invalid control structure");
                env->compile_error = 1;
                return;
            }
            return;
        }
        else if (strcmp(token, "BEGIN") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_BEGIN;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_BEGIN, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "WHILE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error("WHILE without BEGIN");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_WHILE;
            instr.operand = 0;
            env->control_stack[env->control_stack_top - 1].type = CT_WHILE;
            env->control_stack[env->control_stack_top - 1].addr = env->currentWord.code_length;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "REPEAT") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_WHILE) {
                set_error("REPEAT without WHILE");
                env->compile_error = 1;
                return;
            }
            ControlEntry while_entry = env->control_stack[env->control_stack_top - 1];
            env->currentWord.code[while_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_REPEAT;
            instr.operand = while_entry.addr - 8;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            env->control_stack_top--;
            return;
        }
        else if (strcmp(token, "UNTIL") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error("UNTIL without BEGIN");
                env->compile_error = 1;
                return;
            }
            ControlEntry begin_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_UNTIL;
            instr.operand = begin_entry.addr;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "CASE") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error("Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_CASE;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_CASE, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "OF") == 0) {
            int case_found = 0;
            for (int i = 0; i < env->control_stack_top; i++) {
                if (env->control_stack[i].type == CT_CASE) {
                    case_found = 1;
                    break;
                }
            }
            if (!case_found) {
                set_error("OF without CASE");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_OF;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_OF, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ENDOF") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_OF) {
                set_error("ENDOF without OF");
                env->compile_error = 1;
                return;
            }
            ControlEntry of_entry = env->control_stack[--env->control_stack_top];
            env->currentWord.code[of_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_ENDOF;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_ENDOF, env->currentWord.code_length};
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ENDCASE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_ENDOF) {
                set_error("ENDCASE without ENDOF or CASE");
                env->compile_error = 1;
                return;
            }
            while (env->control_stack_top > 0 && env->control_stack[env->control_stack_top - 1].type == CT_ENDOF) {
                ControlEntry endof_entry = env->control_stack[--env->control_stack_top];
                env->currentWord.code[endof_entry.addr].operand = env->currentWord.code_length;
            }
            if (env->control_stack_top > 0 && env->control_stack[env->control_stack_top - 1].type == CT_CASE) {
                env->control_stack_top--;
            } else {
                set_error("ENDCASE without matching CASE");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_ENDCASE;
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else {
            int index = findCompiledWordIndex(token);
            if (index >= 0) {
                instr.opcode = OP_CALL;
                instr.operand = index;
            } else {
                mpz_t test_num;
                mpz_init(test_num);
                if (mpz_set_str(test_num, token, 10) == 0) {
                    instr.opcode = OP_PUSH;
                    instr.operand = env->currentWord.string_count;
                    env->currentWord.strings[env->currentWord.string_count++] = strdup(token);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Unknown word in definition: %s", token);
                    send_to_channel(msg);
                    env->compile_error = 1;
                    mpz_clear(test_num);
                    return;
                }
                mpz_clear(test_num);
            }
        }
        if (env->currentWord.code_length < WORD_CODE_SIZE) {
            env->currentWord.code[env->currentWord.code_length++] = instr;
        } else {
            set_error("Word code size exceeded");
            env->compile_error = 1;
        }
        return;
    }

    // Mode interprétation
    else {
        if (strcmp(token, "LOAD") == 0) {
            char *next_token = strtok_r(NULL, "\"", input_rest);
            if (!next_token || *next_token == '\0') {
                set_error("LOAD: No filename provided");
                return;
            }
            while (*next_token == ' ' || *next_token == '\t') next_token++;
            if (*next_token == '\0') {
                set_error("LOAD: No filename provided");
                return;
            }
            char filename[512];
            strncpy(filename, next_token, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
            FILE *file = fopen(filename, "r");
            if (file) {
                char buffer[1024];
                while (fgets(buffer, sizeof(buffer), file)) {
                    buffer[strcspn(buffer, "\n")] = '\0';
                    interpret(buffer, &env->main_stack);
                }
                fclose(file);
            } else {
                char error_msg[1024];
                snprintf(error_msg, sizeof(error_msg), "Error: LOAD: Cannot open file '%s'", filename);
                set_error(error_msg);
            }
            strtok_r(NULL, " \t\n", input_rest);
            return;
        }
        else if (strcmp(token, "CREATE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("CREATE requires a name");
                env->compile_error = 1;
                return;
            }
            int existing_idx = findCompiledWordIndex(next_token);
            unsigned long index = memory_create(&env->memory_list, next_token, TYPE_ARRAY);
            if (index == 0) {
                set_error("CREATE: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                word->name = strdup(next_token);
                word->code[0].opcode = OP_PUSH;
                word->code[0].operand = index;
                word->code_length = 1;
                word->string_count = 0;
                word->immediate = 0;
                MemoryNode *node = memory_get(&env->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)malloc(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
                MemoryNode *node = memory_get(&env->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)malloc(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            }
            return;
        }
        else if (strcmp(token, "VARIABLE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error("VARIABLE requires a name");
                return;
            }
            int existing_idx = findCompiledWordIndex(next_token);
            unsigned long encoded_index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (encoded_index == 0) {
                set_error("VARIABLE: Memory creation failed");
                return;
            }
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                word->name = strdup(next_token);
                word->code[0].opcode = OP_PUSH;
                word->code[0].operand = encoded_index;
                word->code_length = 1;
                word->string_count = 0;
                word->immediate = 0;
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = encoded_index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
            }
            return;
        }
        else if (strcmp(token, ".\"") == 0) {
            char *next_token = strtok_r(NULL, "\"", input_rest);
            if (!next_token) {
                set_error(".\" expects a string ending with \"");
                return;
            }
            send_to_channel(next_token);
            strtok_r(NULL, " \t\n", input_rest);
            return;
        }
        else if (strcmp(token, "\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error("Missing closing quote for \"");
                return;
            }
            long int len = end - start;
            char *str = malloc(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            push_string(str);
            mpz_set_si(mpz_pool[0], env->string_stack_top);
            push(&env->main_stack, mpz_pool[0]);
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (next_token) compileToken(next_token, input_rest, env);
            return;
        }
else if (strcmp(token, "CONSTANT") == 0) {
     ;
    char *next_token = strtok_r(NULL, " \t\n", input_rest);
    if (!next_token) {
        set_error("CONSTANT requires a name");
        env->compile_error = 1;
        return;
    }
    if (env->main_stack.top < 0) {
        set_error("CONSTANT: Stack underflow");
        env->compile_error = 1;
        return;
    }
    mpz_t value;
    pop(&env->main_stack, value);
    int existing_idx = findCompiledWordIndex(next_token);
    if (existing_idx >= 0) {
        CompiledWord *word = &env->dictionary.words[existing_idx];
        if (word->name) free(word->name);
        for (int j = 0; j < word->string_count; j++) {
            if (word->strings[j]) free(word->strings[j]);
        }
        word->name = strdup(next_token);
        word->code[0].opcode = OP_CONSTANT;
        word->code[0].operand = mpz_get_ui(value);
        word->code_length = 1;
        word->string_count = 0;
        word->immediate = 0;
    } else {
        if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
        int dict_idx = env->dictionary.count++;
        env->dictionary.words[dict_idx].name = strdup(next_token);
        env->dictionary.words[dict_idx].code[0].opcode = OP_CONSTANT;
        env->dictionary.words[dict_idx].code[0].operand = mpz_get_ui(value);
        env->dictionary.words[dict_idx].code_length = 1;
        env->dictionary.words[dict_idx].string_count = 0;
        env->dictionary.words[dict_idx].immediate = 0;
    }
     
    *input_rest = NULL; // Doit arrêter le parsing
    return;
}
        else {
            int idx = findCompiledWordIndex(token);
            if (idx >= 0) {
                executeCompiledWord(&env->dictionary.words[idx], &env->main_stack, idx);
            } else {
                mpz_t test_num;
                mpz_init(test_num);
                if (mpz_set_str(test_num, token, 10) == 0) {
                    push(&env->main_stack, test_num);
                } else {
                    char msg[512];
                    snprintf(msg, sizeof(msg), "Unknown word: %s", token);
                    send_to_channel(msg);
                }
                mpz_clear(test_num);
            }
            return;
        }
    }
}
