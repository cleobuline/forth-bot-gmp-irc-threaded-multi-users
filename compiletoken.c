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

// Fonctions utilitaires pour redimensionner les tableaux dynamiques
static void resizeCodeArray(Env* env, CompiledWord *word) {
    long int new_capacity = word->code_capacity ? word->code_capacity * 2 : 1;
    Instruction *new_code = (Instruction *)SAFE_REALLOC(word->code, new_capacity * sizeof(Instruction));
    if (!new_code) {
        set_error(env, "Failed to resize code array");
        return;
    }
    word->code = new_code;
    word->code_capacity = new_capacity;
}

static void resizeStringArray(Env *env, CompiledWord *word) {
    long int new_capacity = word->string_capacity ? word->string_capacity * 2 : 1;
    char **new_strings = (char **)SAFE_REALLOC(word->strings, new_capacity * sizeof(char *));
    if (!new_strings) {
        set_error(env, "Failed to resize string array");
        return;
    }
    word->strings = new_strings;
    word->string_capacity = new_capacity;
}

void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index, Env *env) {
    long int ip = 0;
    while (ip < word->code_length && !env->error_flag) {
        executeInstruction(word->code[ip], stack, &ip, word, word_index, env);
        ip++;
    }
}

void compileToken(char *token, char **input_rest, Env *env) {
    Instruction instr = {0};
    if (!env || env->compile_error) return;

    // Mots immédiats
    int idx = findCompiledWordIndex(token, env);
    if (idx >= 0 && env->dictionary.words[idx].immediate) {
        if (strcmp(token, "STRING") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error(env, "STRING requires a name");
                env->compile_error = 1;
                return;
            }
            if (findCompiledWordIndex(next_token, env) >= 0) {
                char msg[512];
                snprintf(msg, sizeof(msg), "STRING: '%s' already defined", next_token);
                set_error(env, msg);
                env->compile_error = 1;
                return;
            }
            unsigned long index = memory_create(&env->memory_list, next_token, TYPE_STRING);
            if (index == 0) {
                set_error(env, "STRING: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
            int dict_idx = env->dictionary.count++;
            env->dictionary.words[dict_idx].name = strdup(next_token);
            env->dictionary.words[dict_idx].code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
            env->dictionary.words[dict_idx].code_capacity = 1;
            env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
            env->dictionary.words[dict_idx].code[0].operand = index;
            env->dictionary.words[dict_idx].code_length = 1;
            env->dictionary.words[dict_idx].strings = (char **)SAFE_MALLOC(sizeof(char *));
            env->dictionary.words[dict_idx].string_capacity = 1;
            env->dictionary.words[dict_idx].string_count = 0;
            env->dictionary.words[dict_idx].immediate = 0;
            if (env->compiling) {
                instr.opcode = OP_STRING;
                instr.operand = env->currentWord.string_count;
                if (env->currentWord.string_count >= env->currentWord.string_capacity) {
                    resizeStringArray(env, &env->currentWord);
                }
                env->currentWord.strings[env->currentWord.string_count++] = strdup(next_token);
                if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                    resizeCodeArray(env, &env->currentWord);
                }
                env->currentWord.code[env->currentWord.code_length++] = instr;
            }
        } else if (strcmp(token, "FORGET") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error(env, "FORGET requires a word name");
                env->compile_error = 1;
                return;
            }
            CompiledWord temp_word = {0};
            temp_word.strings = (char **)SAFE_MALLOC(sizeof(char *));
            temp_word.string_capacity = 1;
            temp_word.strings[0] = strdup(next_token);
            temp_word.string_count = 1;
            instr.opcode = OP_FORGET;
            instr.operand = 0;
            executeInstruction(instr, &env->main_stack, NULL, &temp_word, -1, env);
            free(temp_word.strings[0]);
            free(temp_word.strings);
            return;
        }
        return;
    }

    // Début d’une définition
    if (strcmp(token, ":") == 0) {
        char *next_token = strtok_r(NULL, " \t\n", input_rest);
        if (!next_token) {
            set_error(env, "No name for definition");
            env->compile_error = 1;
            return;
        }
        env->compiling = 1;
        int existing_idx = findCompiledWordIndex(next_token, env);
        if (existing_idx >= 0) {
            env->current_word_index = existing_idx;
            freeCurrentWord(env);  // Libère l’ancienne définition
        } else {
            env->current_word_index = env->dictionary.count;
            if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
            env->dictionary.count++;
        }
        env->currentWord.name = strdup(next_token);
        env->currentWord.code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
        env->currentWord.code_capacity = 1;
        env->currentWord.code_length = 0;
        env->currentWord.strings = (char **)SAFE_MALLOC(sizeof(char *));
        env->currentWord.string_capacity = 1;
        env->currentWord.string_count = 0;
        env->currentWord.immediate = 0;
        if (!env->currentWord.name || !env->currentWord.code || !env->currentWord.strings) {
            set_error(env, "Memory allocation failed in definition");
            env->compile_error = 1;
            freeCurrentWord(env);
            return;
        }
        env->control_stack_top = 0;
        return;
    }

    // Fin d’une définition
    if (strcmp(token, ";") == 0) {
        if (!env->compiling) {
            set_error(env, "Extra ;");
            env->compile_error = 1;
            return;
        }
        if (env->control_stack_top > 0) {
            set_error(env, "Unmatched control structures");
            env->compile_error = 1;
            env->control_stack_top = 0;
            env->compiling = 0;
            return;
        }
        instr.opcode = OP_END;
        if (env->currentWord.code_length >= env->currentWord.code_capacity) {
            resizeCodeArray(env, &env->currentWord);
        }
        env->currentWord.code[env->currentWord.code_length++] = instr;
        
        if (env->current_word_index >= 0 && env->current_word_index < env->dictionary.capacity) {
            CompiledWord *dict_word = &env->dictionary.words[env->current_word_index];
            if (dict_word->name) free(dict_word->name);
            if (dict_word->code) free(dict_word->code);
            for (int j = 0; j < dict_word->string_count; j++) {
                if (dict_word->strings[j]) free(dict_word->strings[j]);
            }
            if (dict_word->strings) free(dict_word->strings);
            
            dict_word->name = env->currentWord.name;
            dict_word->code = env->currentWord.code;
            dict_word->code_length = env->currentWord.code_length;
            dict_word->code_capacity = env->currentWord.code_capacity;
            dict_word->strings = env->currentWord.strings;
            dict_word->string_count = env->currentWord.string_count;
            dict_word->string_capacity = env->currentWord.string_capacity;
            dict_word->immediate = env->currentWord.immediate;
        } else {
            set_error(env, "Dictionary index out of bounds");
            env->compile_error = 1;
            if (env->currentWord.name) free(env->currentWord.name);
            if (env->currentWord.code) free(env->currentWord.code);
            for (int j = 0; j < env->currentWord.string_count; j++) {
                if (env->currentWord.strings[j]) free(env->currentWord.strings[j]);
            }
            if (env->currentWord.strings) free(env->currentWord.strings);
        }
        
        env->compiling = 0;
        env->compile_error = 0;
        env->control_stack_top = 0;
        env->currentWord.name = NULL;
        env->currentWord.code = NULL;
        env->currentWord.code_length = 0;
        env->currentWord.code_capacity = 0;
        env->currentWord.strings = NULL;
        env->currentWord.string_count = 0;
        env->currentWord.string_capacity = 0;
        env->currentWord.immediate = 0;
        return;
    }
 
    // Récursion
    if (strcmp(token, "RECURSE") == 0) {
        if (!env->compiling) {
            set_error(env, "RECURSE outside definition");
            env->compile_error = 1;
            return;
        }
        instr.opcode = OP_CALL;
        instr.operand = env->current_word_index;
        if (env->currentWord.code_length >= env->currentWord.code_capacity) {
            resizeCodeArray(env, &env->currentWord);
        }
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
        int index = findCompiledWordIndex(next_token, env);
        if (index >= 0) {
            print_word_definition_irc(index, &env->main_stack, env);
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "SEE: Unknown word: %s", next_token);
            send_to_channel(msg);
            env->compile_error = 1;
        }
        return;
    }

    // Mode compilation
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
                set_error(env, "Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_DO;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_DO, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "LOOP") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_DO) {
                set_error(env, "LOOP without DO");
                env->compile_error = 1;
                return;
            }
            ControlEntry do_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_LOOP;
            instr.operand = do_entry.addr + 1;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "+LOOP") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_DO) {
                set_error(env, "+LOOP without DO");
                env->compile_error = 1;
                return;
            }
            ControlEntry do_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_PLUS_LOOP;
            instr.operand = do_entry.addr + 1;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
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
        else if (strcmp(token, "APPEND") == 0) instr.opcode = OP_APPEND;
        else if (strcmp(token, ".\"") == 0) {
            char *start = *input_rest;
            char *end = strchr(start, '"');
            if (!end) {
                set_error(env, ".\" expects a string ending with \"");
                env->compile_error = 1;
                return;
            }
            long int len = end - start;
            char *str = SAFE_MALLOC(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            instr.opcode = OP_DOT_QUOTE;
            instr.operand = env->currentWord.string_count;
            if (env->currentWord.string_count >= env->currentWord.string_capacity) {
                resizeStringArray(env, &env->currentWord);
            }
            env->currentWord.strings[env->currentWord.string_count] = str;
            env->currentWord.string_count++;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            *input_rest = end + 1;
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            return;
        }
        else if (strcmp(token, "VARIABLE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error(env, "VARIABLE requires a name");
                env->compile_error = 1;
                return;
            }
            int existing_idx = findCompiledWordIndex(next_token, env);
            unsigned long encoded_index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (encoded_index == 0) {
                set_error(env, "VARIABLE: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                if (word->code) free(word->code);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                if (word->strings) free(word->strings);
                word->name = strdup(next_token);
                word->code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
                word->code_capacity = 1;
                word->code[0].opcode = OP_PUSH;
                word->code[0].operand = encoded_index;
                word->code_length = 1;
                word->strings = (char **)SAFE_MALLOC(sizeof(char *));
                word->string_capacity = 1;
                word->string_count = 0;
                word->immediate = 0;
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
                env->dictionary.words[dict_idx].code_capacity = 1;
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = encoded_index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].strings = (char **)SAFE_MALLOC(sizeof(char *));
                env->dictionary.words[dict_idx].string_capacity = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
            }
            return;
        }
        else if (strcmp(token, "S") == 0) {
            char *start = *input_rest;
            char *end = strstr(start, " S ");
            if (!end) {
                set_error(env, "S expects a string ending with S");
                env->compile_error = 1;
                return;
            }
            long int len = end - start;
            char *str = SAFE_MALLOC(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            instr.opcode = OP_QUOTE;
            instr.operand = env->currentWord.string_count;
            if (env->currentWord.string_count >= env->currentWord.string_capacity) {
                resizeStringArray(env, &env->currentWord);
            }
            env->currentWord.strings[env->currentWord.string_count++] = str;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            *input_rest = end + 3; // Passe " S " (2 caractères pour " S" + 1 espace)
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            return;
        }
        else if (strcmp(token, "IF") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error(env, "Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_BRANCH_FALSE;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_IF, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ELSE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_IF) {
                set_error(env, "ELSE without IF");
                env->compile_error = 1;
                return;
            }
            ControlEntry if_entry = env->control_stack[env->control_stack_top - 1];
            env->currentWord.code[if_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_BRANCH;
            instr.operand = 0;
            env->control_stack[env->control_stack_top - 1].type = CT_ELSE;
            env->control_stack[env->control_stack_top - 1].addr = env->currentWord.code_length;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "THEN") == 0) {
            if (env->control_stack_top <= 0) {
                set_error(env, "THEN without IF or ELSE");
                env->compile_error = 1;
                return;
            }
            ControlEntry entry = env->control_stack[--env->control_stack_top];
            if (entry.type == CT_IF || entry.type == CT_ELSE) {
                env->currentWord.code[entry.addr].operand = env->currentWord.code_length;
            } else {
                set_error(env, "Invalid control structure");
                env->compile_error = 1;
                return;
            }
            return;
        }
        else if (strcmp(token, "BEGIN") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error(env, "Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_BEGIN;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_BEGIN, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "WHILE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error(env, "WHILE without BEGIN");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_WHILE;
            instr.operand = 0;
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error(env, "Control stack overflow");
                env->compile_error = 1;
                return;
            }
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_WHILE, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "REPEAT") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_WHILE) {
                set_error(env, "REPEAT without WHILE");
                env->compile_error = 1;
                return;
            }
            ControlEntry while_entry = env->control_stack[env->control_stack_top - 1];
            env->currentWord.code[while_entry.addr].operand = env->currentWord.code_length + 1;
            if (env->control_stack_top <= 1 || env->control_stack[env->control_stack_top - 2].type != CT_BEGIN) {
                set_error(env, "REPEAT without BEGIN");
                env->compile_error = 1;
                return;
            }
            ControlEntry begin_entry = env->control_stack[env->control_stack_top - 2];
            instr.opcode = OP_REPEAT;
            instr.operand = begin_entry.addr;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            env->control_stack_top -= 2;
            return;
        }
        else if (strcmp(token, "UNTIL") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error(env, "UNTIL without BEGIN");
                env->compile_error = 1;
                return;
            }
            ControlEntry begin_entry = env->control_stack[--env->control_stack_top];
            instr.opcode = OP_UNTIL;
            instr.operand = begin_entry.addr;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "AGAIN") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_BEGIN) {
                set_error(env, "AGAIN without BEGIN");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_AGAIN;
            instr.operand = env->control_stack[env->control_stack_top - 1].addr;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            env->control_stack_top--;
            return;
        }
        else if (strcmp(token, "CASE") == 0) {
            if (env->control_stack_top >= CONTROL_STACK_SIZE) {
                set_error(env, "Control stack overflow");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_CASE;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_CASE, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
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
                set_error(env, "OF without CASE");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_OF;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_OF, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ENDOF") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_OF) {
                set_error(env, "ENDOF without OF");
                env->compile_error = 1;
                return;
            }
            ControlEntry of_entry = env->control_stack[--env->control_stack_top];
            env->currentWord.code[of_entry.addr].operand = env->currentWord.code_length + 1;
            instr.opcode = OP_ENDOF;
            instr.operand = 0;
            env->control_stack[env->control_stack_top++] = (ControlEntry){CT_ENDOF, env->currentWord.code_length};
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else if (strcmp(token, "ENDCASE") == 0) {
            if (env->control_stack_top <= 0 || env->control_stack[env->control_stack_top - 1].type != CT_ENDOF) {
                set_error(env, "ENDCASE without ENDOF or CASE");
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
                set_error(env, "ENDCASE without matching CASE");
                env->compile_error = 1;
                return;
            }
            instr.opcode = OP_ENDCASE;
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        else {
            int index = findCompiledWordIndex(token, env);
            if (index >= 0) {
                instr.opcode = OP_CALL;
                instr.operand = index;
            } else {
                mpz_t test_num;
                mpz_init(test_num);
                if (mpz_set_str(test_num, token, 10) == 0) {
                    instr.opcode = OP_PUSH;
                    instr.operand = env->currentWord.string_count;
                    if (env->currentWord.string_count >= env->currentWord.string_capacity) {
                        resizeStringArray(env, &env->currentWord);
                    }
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
            if (env->currentWord.code_length >= env->currentWord.code_capacity) {
                resizeCodeArray(env, &env->currentWord);
            }
            env->currentWord.code[env->currentWord.code_length++] = instr;
            return;
        }
        if (env->currentWord.code_length >= env->currentWord.code_capacity) {
            resizeCodeArray(env, &env->currentWord);
        }
        env->currentWord.code[env->currentWord.code_length++] = instr;
        return;
    }

    // Mode interprétation
    else {
        if (strcmp(token, "LOAD") == 0) {
            char *next_token = strtok_r(NULL, "\"", input_rest);
            if (!next_token || *next_token == '\0') {
                set_error(env, "LOAD: No filename provided");
                return;
            }
            while (*next_token == ' ' || *next_token == '\t') next_token++;
            if (*next_token == '\0') {
                set_error(env, "LOAD: No filename provided");
                return;
            }
            char filename[512];
            strncpy(filename, next_token, sizeof(filename) - 1);
            filename[sizeof(filename) - 1] = '\0';
            FILE *file = fopen(filename, "r");
            if (file) {
                char full_buffer[8000] = "";
                char line_buffer[1024];
                size_t full_len = 0;
                int compiling = 0;

                while (fgets(line_buffer, sizeof(line_buffer), file)) {
                    line_buffer[strcspn(line_buffer, "\n")] = '\0';
                    size_t line_len = strlen(line_buffer);
                    if (full_len + line_len + 1 >= sizeof(full_buffer)) {
                        set_error(env, "LOAD: File too large for buffer");
                        break;
                    }
                    if (full_len > 0) {
                        full_buffer[full_len++] = ' ';
                    }
                    strcpy(full_buffer + full_len, line_buffer);
                    full_len += line_len;
                    if (strstr(line_buffer, ";")) {
                        if (!compiling || (compiling && env->control_stack_top == 0)) {
                            interpret(full_buffer, &env->main_stack, env);
                            full_buffer[0] = '\0';
                            full_len = 0;
                            compiling = 0;
                        }
                    } else if (strstr(line_buffer, ":")) {
                        compiling = 1;
                    }
                }
                if (full_len > 0) {
                    interpret(full_buffer, &env->main_stack, env);
                }
                fclose(file);
            } else {
                char error_msg[1024];
                snprintf(error_msg, sizeof(error_msg), "Error: LOAD: Cannot open file '%s'", filename);
                set_error(env, error_msg);
            }
            strtok_r(NULL, " \t\n", input_rest);
            return;
        }
        else if (strcmp(token, "CREATE") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error(env, "CREATE requires a name");
                env->compile_error = 1;
                return;
            }
            int existing_idx = findCompiledWordIndex(next_token, env);
            unsigned long index = memory_create(&env->memory_list, next_token, TYPE_ARRAY);
            if (index == 0) {
                set_error(env, "CREATE: Memory creation failed");
                env->compile_error = 1;
                return;
            }
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                if (word->code) free(word->code);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                if (word->strings) free(word->strings);
                word->name = strdup(next_token);
                word->code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
                word->code_capacity = 1;
                word->code[0].opcode = OP_PUSH;
                word->code[0].operand = index;
                word->code_length = 1;
                word->strings = (char **)SAFE_MALLOC(sizeof(char *));
                word->string_capacity = 1;
                word->string_count = 0;
                word->immediate = 0;
                MemoryNode *node = memory_get(&env->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)SAFE_MALLOC(sizeof(mpz_t));
                    mpz_init(node->value.array.data[0]);
                    mpz_set_ui(node->value.array.data[0], 0);
                    node->value.array.size = 1;
                }
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
                env->dictionary.words[dict_idx].code_capacity = 1;
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].strings = (char **)SAFE_MALLOC(sizeof(char *));
                env->dictionary.words[dict_idx].string_capacity = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
                MemoryNode *node = memory_get(&env->memory_list, index);
                if (node && node->type == TYPE_ARRAY) {
                    node->value.array.data = (mpz_t *)SAFE_MALLOC(sizeof(mpz_t));
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
                set_error(env, "VARIABLE requires a name");
                return;
            }
            int existing_idx = findCompiledWordIndex(next_token, env);
            unsigned long encoded_index = memory_create(&env->memory_list, next_token, TYPE_VAR);
            if (encoded_index == 0) {
                set_error(env, "VARIABLE: Memory creation failed");
                return;
            }
            if (existing_idx >= 0) {
                CompiledWord *word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                if (word->code) free(word->code);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                if (word->strings) free(word->strings);
                word->name = strdup(next_token);
                word->code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
                word->code_capacity = 1;
                word->code[0].opcode = OP_PUSH;
                word->code[0].operand = encoded_index;
                word->code_length = 1;
                word->strings = (char **)SAFE_MALLOC(sizeof(char *));
                word->string_capacity = 1;
                word->string_count = 0;
                word->immediate = 0;
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                int dict_idx = env->dictionary.count++;
                env->dictionary.words[dict_idx].name = strdup(next_token);
                env->dictionary.words[dict_idx].code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
                env->dictionary.words[dict_idx].code_capacity = 1;
                env->dictionary.words[dict_idx].code[0].opcode = OP_PUSH;
                env->dictionary.words[dict_idx].code[0].operand = encoded_index;
                env->dictionary.words[dict_idx].code_length = 1;
                env->dictionary.words[dict_idx].strings = (char **)SAFE_MALLOC(sizeof(char *));
                env->dictionary.words[dict_idx].string_capacity = 1;
                env->dictionary.words[dict_idx].string_count = 0;
                env->dictionary.words[dict_idx].immediate = 0;
            }
            return;
        }
        else if (strcmp(token, ".\"") == 0) {
            char *next_token = strtok_r(NULL, "\"", input_rest);
            if (!next_token) {
                set_error(env, ".\" expects a string ending with \"");
                return;
            }
            send_to_channel(next_token);
            strtok_r(NULL, " \t\n", input_rest);
            return;
        }
        else if (strcmp(token, "S\"") == 0) {
            char *start = *input_rest;
            char *end = strstr(start, "\"S");
            if (!end) {
                set_error(env, " S\" expects a string ending with \"S");
                return;
            }
            long int len = end - start;
            char *str = SAFE_MALLOC(len + 1);
            strncpy(str, start, len);
            str[len] = '\0';
            push_string(env, str);
            mpz_set_si(env->mpz_pool[0], env->string_stack_top);
            push(env, env->mpz_pool[0]);
            *input_rest = end + 3; // Passe " S " (2 caractères pour " S" )
            while (**input_rest == ' ' || **input_rest == '\t') (*input_rest)++;
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (next_token) compileToken(next_token, input_rest, env);
            return;
        }
        else if (strcmp(token, "CONSTANT") == 0) {
            char *next_token = strtok_r(NULL, " \t\n", input_rest);
            if (!next_token) {
                set_error(env, "CONSTANT requires a name");
                env->compile_error = 1;
                return;
            }
            if (env->main_stack.top < 0) {
                set_error(env, "CONSTANT: Stack underflow");
                env->compile_error = 1;
                return;
            }
            mpz_t value;
            mpz_init(value);
            pop(env, value);
            int existing_idx = findCompiledWordIndex(next_token, env);
            CompiledWord *word;
            int dict_idx;

            if (existing_idx >= 0) {
                word = &env->dictionary.words[existing_idx];
                if (word->name) free(word->name);
                if (word->code) free(word->code);
                for (int j = 0; j < word->string_count; j++) {
                    if (word->strings[j]) free(word->strings[j]);
                }
                if (word->strings) free(word->strings);
            } else {
                if (env->dictionary.count >= env->dictionary.capacity) resizeDynamicDictionary(&env->dictionary);
                dict_idx = env->dictionary.count++;
                word = &env->dictionary.words[dict_idx];
            }

            word->name = strdup(next_token);
            if (!word->name) {
                mpz_clear(value);
                set_error(env, "CONSTANT: Memory allocation failed for name");
                env->compile_error = 1;
                return;
            }
            word->code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
            if (!word->code) {
                free(word->name);
                mpz_clear(value);
                set_error(env, "CONSTANT: Memory allocation failed for code");
                env->compile_error = 1;
                return;
            }
            word->strings = (char **)SAFE_MALLOC(sizeof(char *));
            if (!word->strings) {
                free(word->name);
                free(word->code);
                mpz_clear(value);
                set_error(env, "CONSTANT: Memory allocation failed for strings");
                env->compile_error = 1;
                return;
            }

            word->code_capacity = 1;
            word->code[0].opcode = OP_CONSTANT;
            word->code[0].operand = mpz_get_ui(value);
            word->code_length = 1;
            word->string_capacity = 1;
            word->string_count = 0;
            word->immediate = 0;

            mpz_clear(value);
            *input_rest = NULL;
            return;
        }
        else if (strcmp(token, "APPEND") == 0) {
            instr.opcode = OP_APPEND;
            executeInstruction(instr, &env->main_stack, NULL, NULL, -1, env);
            return;
        }
        else {
            int idx = findCompiledWordIndex(token, env);
            if (idx >= 0) {
                executeCompiledWord(&env->dictionary.words[idx], &env->main_stack, idx, env);
            } else {
                mpz_t test_num;
                mpz_init(test_num);
                if (mpz_set_str(test_num, token, 10) == 0) {
                    push(env, test_num);
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
