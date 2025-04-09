#include <pthread.h>
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
#include <netdb.h>
#include <curl/curl.h>
#include "memory_forth.h"
#include "forth_bot.h"


void initDynamicDictionary(DynamicDictionary *dict) {
    dict->capacity = 128;  // Plus grand pour éviter les redimensionnements précoces
    dict->count = 0;
    dict->words = (CompiledWord *)calloc(dict->capacity, sizeof(CompiledWord));
    if (!dict->words) {
        send_to_channel("Erreur : Échec de l’allocation du dictionnaire");
        exit(1);
    }

    for (long int i = 0; i < dict->capacity; i++) {
        dict->words[i].name = NULL;
        dict->words[i].code_length = 0;
        dict->words[i].code_capacity = 16;
        dict->words[i].code = (Instruction *)calloc(dict->words[i].code_capacity, sizeof(Instruction));
        dict->words[i].string_count = 0;
        dict->words[i].string_capacity = 16;
        dict->words[i].strings = (char **)calloc(dict->words[i].string_capacity, sizeof(char *));
        dict->words[i].immediate = 0;

        if (!dict->words[i].code || !dict->words[i].strings) {
            send_to_channel("Erreur : Échec de l’initialisation des tableaux dans CompiledWord");
            for (long int j = 0; j < i; j++) {
                free(dict->words[j].code);
                free(dict->words[j].strings);
            }
            free(dict->words);
            exit(1);
        }
    }
}
 
 
void resizeCompiledWordArrays(CompiledWord *word, int is_code) {
    if (is_code) {
        long int new_capacity = word->code_capacity * 2;
        Instruction *new_code = (Instruction *)realloc(word->code, new_capacity * sizeof(Instruction));
        if (!new_code) {
            send_to_channel("Erreur : Échec du redimensionnement du tableau code");
            exit(1);
        }
        memset(new_code + word->code_capacity, 0, (new_capacity - word->code_capacity) * sizeof(Instruction));
        word->code = new_code;
        word->code_capacity = new_capacity;
    } else {
        long int new_capacity = word->string_capacity * 2;
        char **new_strings = (char **)realloc(word->strings, new_capacity * sizeof(char *));
        if (!new_strings) {
            send_to_channel("Erreur : Échec du redimensionnement du tableau strings");
            exit(1);
        }
        memset(new_strings + word->string_capacity, 0, (new_capacity - word->string_capacity) * sizeof(char *));
        word->strings = new_strings;
        word->string_capacity = new_capacity;
    }
}
 
void resizeDynamicDictionary(DynamicDictionary *dict) {
    long int new_capacity = dict->capacity * 2;
    CompiledWord *new_words = (CompiledWord *)realloc(dict->words, new_capacity * sizeof(CompiledWord));
    if (!new_words) {
        send_to_channel("Error: Failed to resize dictionary, operation aborted try to FORGET somme WORDS ");
        return;  // Ne pas exit, juste abandonner le redimensionnement
    }
    dict->words = new_words;

    for (long int i = dict->count; i < new_capacity; i++) {
        dict->words[i].name = NULL;
        dict->words[i].code_length = 0;
        dict->words[i].code_capacity = 16;
        dict->words[i].code = (Instruction *)calloc(dict->words[i].code_capacity, sizeof(Instruction));
        dict->words[i].string_count = 0;
        dict->words[i].string_capacity = 16;
        dict->words[i].strings = (char **)calloc(dict->words[i].string_capacity, sizeof(char *));
        dict->words[i].immediate = 0;

        if (!dict->words[i].code || !dict->words[i].strings) {
            send_to_channel("Error: Failed to allocate arrays in resizeDynamicDictionary ");
            for (long int j = dict->count; j < i; j++) {
                free(dict->words[j].code);
                free(dict->words[j].strings);
            }
            return;  // Abandonne sans exit
        }
    }
    dict->capacity = new_capacity;
}
 
 
 
void addWord(DynamicDictionary *dict, const char *name, OpCode opcode, int immediate) {
    if (dict->count >= dict->capacity) {
        resizeDynamicDictionary(dict);
    }
    CompiledWord *word = &dict->words[dict->count];

    if (word->name) free(word->name);
    if (word->code) free(word->code);
    if (word->strings) {
        for (int j = 0; j < word->string_count; j++) {
            if (word->strings[j]) free(word->strings[j]);
        }
        free(word->strings);
    }

    word->name = strdup(name);
    word->code_capacity = 16;
    word->code = (Instruction *)calloc(word->code_capacity, sizeof(Instruction));
    word->code[0].opcode = opcode;
    word->code_length = 1;
    word->string_capacity = 16;
    word->strings = (char **)calloc(word->string_capacity, sizeof(char *));
    word->string_count = 0;
    word->immediate = immediate;

    if (!word->name || !word->code || !word->strings) {
        send_to_channel("Erreur : Échec de l’allocation dans addWord");
        if (word->name) free(word->name);
        if (word->code) free(word->code);
        if (word->strings) free(word->strings);
        return;
    }
    dict->count++;
}
int findCompiledWordIndex(char *name, Env *env) {
    if (!env) return -1;
    for (long int i = 0; i < env->dictionary.count; i++) {
        if (env->dictionary.words[i].name && strcmp(env->dictionary.words[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// A littke bit complex , but usefull to interact with this forth usage SEE THE_WORD ( print the definition ) 

void print_word_definition_irc(int index, Stack *stack, Env *env) {
    if (!env || index < 0 || index >= env->dictionary.count) {
        send_to_channel("SEE: Unknown word");
        return;
    }

    CompiledWord *word = &env->dictionary.words[index];
    char def_msg[512] = "";
    snprintf(def_msg, sizeof(def_msg), ": %s ", word->name);

    long int *branch_targets = (long int *)malloc(word->code_capacity * sizeof(long int));
    if (!branch_targets) {
        send_to_channel("SEE: Memory allocation failed for branch targets");
        return;
    }
    int branch_depth = 0;
    int has_semicolon = 0;

    for (long int i = 0; i < word->code_length; i++) {
        Instruction instr = word->code[i];
        char instr_str[64] = "";

        switch (instr.opcode) {
            case OP_PUSH:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "%s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "%ld ", instr.operand);
                }
                break;
            case OP_CALL:
                if (instr.operand < env->dictionary.count && env->dictionary.words[instr.operand].name) {
                    snprintf(instr_str, sizeof(instr_str), "%s ", env->dictionary.words[instr.operand].name);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "(CALL %ld) ", instr.operand);
                }
                break;
            case OP_ADD: snprintf(instr_str, sizeof(instr_str), "+ "); break;
            case OP_SUB: snprintf(instr_str, sizeof(instr_str), "- "); break;
            case OP_MUL: snprintf(instr_str, sizeof(instr_str), "* "); break;
            case OP_DIV: snprintf(instr_str, sizeof(instr_str), "/ "); break;
            case OP_MOD: snprintf(instr_str, sizeof(instr_str), "MOD "); break;
            case OP_DUP: snprintf(instr_str, sizeof(instr_str), "DUP "); break;
            case OP_DROP: snprintf(instr_str, sizeof(instr_str), "DROP "); break;
            case OP_SWAP: snprintf(instr_str, sizeof(instr_str), "SWAP "); break;
            case OP_OVER: snprintf(instr_str, sizeof(instr_str), "OVER "); break;
            case OP_ROT: snprintf(instr_str, sizeof(instr_str), "ROT "); break;
            case OP_TO_R: snprintf(instr_str, sizeof(instr_str), ">R "); break;
            case OP_FROM_R: snprintf(instr_str, sizeof(instr_str), "R> "); break;
            case OP_R_FETCH: snprintf(instr_str, sizeof(instr_str), "R@ "); break;
            case OP_EQ: snprintf(instr_str, sizeof(instr_str), "= "); break;
            case OP_LT: snprintf(instr_str, sizeof(instr_str), "< "); break;
            case OP_GT: snprintf(instr_str, sizeof(instr_str), "> "); break;
            case OP_AND: snprintf(instr_str, sizeof(instr_str), "AND "); break;
            case OP_OR: snprintf(instr_str, sizeof(instr_str), "OR "); break;
            case OP_NOT: snprintf(instr_str, sizeof(instr_str), "NOT "); break;
            case OP_XOR: snprintf(instr_str, sizeof(instr_str), "XOR "); break;
            case OP_BIT_AND: snprintf(instr_str, sizeof(instr_str), "& "); break;
            case OP_BIT_OR: snprintf(instr_str, sizeof(instr_str), "| "); break;
            case OP_BIT_XOR: snprintf(instr_str, sizeof(instr_str), "^ "); break;
            case OP_BIT_NOT: snprintf(instr_str, sizeof(instr_str), "~ "); break;
            case OP_LSHIFT: snprintf(instr_str, sizeof(instr_str), "<< "); break;
            case OP_RSHIFT: snprintf(instr_str, sizeof(instr_str), ">> "); break;
            case OP_CR: snprintf(instr_str, sizeof(instr_str), "CR "); break;
            case OP_EMIT: snprintf(instr_str, sizeof(instr_str), "EMIT "); break;
            case OP_DOT: snprintf(instr_str, sizeof(instr_str), ". "); break;
            case OP_DOT_S: snprintf(instr_str, sizeof(instr_str), ".S "); break;
            case OP_FETCH: snprintf(instr_str, sizeof(instr_str), "@ "); break;
            case OP_STORE: snprintf(instr_str, sizeof(instr_str), "! "); break;
            case OP_PLUSSTORE: snprintf(instr_str, sizeof(instr_str), "+! "); break;
            case OP_I: snprintf(instr_str, sizeof(instr_str), "I "); break;
            case OP_J: snprintf(instr_str, sizeof(instr_str), "J "); break;
            case OP_DO:
                snprintf(instr_str, sizeof(instr_str), "DO ");
                branch_targets[branch_depth++] = i;
                break;
            case OP_LOOP:
                snprintf(instr_str, sizeof(instr_str), "LOOP ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_PLUS_LOOP:
                snprintf(instr_str, sizeof(instr_str), "+LOOP ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_UNLOOP: snprintf(instr_str, sizeof(instr_str), "UNLOOP "); break;
            case OP_BRANCH:
                snprintf(instr_str, sizeof(instr_str), "BRANCH(%ld) ", instr.operand);
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_BRANCH_FALSE:
                snprintf(instr_str, sizeof(instr_str), "IF ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_CASE:
                snprintf(instr_str, sizeof(instr_str), "CASE ");
                branch_targets[branch_depth++] = i;
                break;
            case OP_OF:
                snprintf(instr_str, sizeof(instr_str), "OF ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_ENDOF:
                snprintf(instr_str, sizeof(instr_str), "ENDOF ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_ENDCASE:
                snprintf(instr_str, sizeof(instr_str), "ENDCASE ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_END:
                if (branch_depth > 0 && i + 1 == branch_targets[branch_depth - 1]) {
                    snprintf(instr_str, sizeof(instr_str), "THEN ");
                    branch_depth--;
                    if (i + 1 == word->code_length) {
                        strncat(instr_str, ";", sizeof(instr_str) - strlen(instr_str) - 1);
                        has_semicolon = 1;
                    }
                } else if (i + 1 == word->code_length) {
                    snprintf(instr_str, sizeof(instr_str), "; ");
                    has_semicolon = 1;
                }
                break;
            case OP_DOT_QUOTE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), ".\" %s \" ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), ".\"(invalid) ");
                }
                break;
            case OP_VARIABLE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "VARIABLE %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "VARIABLE(invalid) ");
                }
                break;
            case OP_STRING:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "STRING %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "STRING(invalid) ");
                }
                break;
            case OP_QUOTE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "\" %s \" ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "\"(invalid) ");
                }
                break;
            case OP_PRINT: snprintf(instr_str, sizeof(instr_str), "PRINT "); break;
            case OP_NUM_TO_BIN: snprintf(instr_str, sizeof(instr_str), "NUM-TO-BIN "); break;
            case OP_PRIME_TEST: snprintf(instr_str, sizeof(instr_str), "PRIME? "); break;
            case OP_BEGIN:
                snprintf(instr_str, sizeof(instr_str), "BEGIN ");
                branch_targets[branch_depth++] = i;
                break;
            case OP_WHILE:
                snprintf(instr_str, sizeof(instr_str), "WHILE ");
                branch_targets[branch_depth++] = instr.operand;
                break;
            case OP_REPEAT:
                snprintf(instr_str, sizeof(instr_str), "REPEAT ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_UNTIL:
                snprintf(instr_str, sizeof(instr_str), "UNTIL ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_AGAIN:
                snprintf(instr_str, sizeof(instr_str), "AGAIN ");
                if (branch_depth > 0) branch_depth--;
                break;
            case OP_EXIT:
                snprintf(instr_str, sizeof(instr_str), "EXIT ");
                break;
            case OP_WORDS: snprintf(instr_str, sizeof(instr_str), "WORDS "); break;
            case OP_FORGET:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "FORGET %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "FORGET(invalid) ");
                }
                break;
            case OP_LOAD:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "LOAD %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "LOAD(invalid) ");
                }
                break;
            case OP_ALLOT: snprintf(instr_str, sizeof(instr_str), "ALLOT "); break;
            case OP_CREATE:
                if (instr.operand < word->string_count && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "CREATE %s ", word->strings[instr.operand]);
                } else {
                    snprintf(instr_str, sizeof(instr_str), "CREATE(invalid) ");
                }
                break;
            case OP_RECURSE: snprintf(instr_str, sizeof(instr_str), "RECURSE "); break;
            case OP_SQRT: snprintf(instr_str, sizeof(instr_str), "SQRT "); break;
            case OP_PICK: snprintf(instr_str, sizeof(instr_str), "PICK "); break;
            case OP_ROLL: snprintf(instr_str, sizeof(instr_str), "ROLL "); break;
            case OP_DEPTH: snprintf(instr_str, sizeof(instr_str), "DEPTH "); break;
            case OP_TOP: snprintf(instr_str, sizeof(instr_str), "TOP "); break;
            case OP_NIP: snprintf(instr_str, sizeof(instr_str), "NIP "); break;
            case OP_CLEAR_STACK: snprintf(instr_str, sizeof(instr_str), "CLEAR-STACK "); break;
            case OP_CLOCK: snprintf(instr_str, sizeof(instr_str), "CLOCK "); break;
            case OP_SEE: snprintf(instr_str, sizeof(instr_str), "SEE "); break;
            case OP_2DROP: snprintf(instr_str, sizeof(instr_str), "2DROP "); break;
            case OP_CONSTANT: snprintf(instr_str, sizeof(instr_str), "%ld ", instr.operand);  break;
            case OP_MICRO: snprintf(instr_str, sizeof(instr_str), "MICRO "); break;
			case OP_MILLI: snprintf(instr_str, sizeof(instr_str), "MILLI "); break;
            default:
                snprintf(instr_str, sizeof(instr_str), "(OP_%d %ld) ", instr.opcode, instr.operand);
                break;
        }

        if (strlen(def_msg) + strlen(instr_str) >= sizeof(def_msg) - 1) {
            send_to_channel(def_msg);
            snprintf(def_msg, sizeof(def_msg), "%s ", instr_str);
        } else {
            strncat(def_msg, instr_str, sizeof(def_msg) - strlen(def_msg) - 1);
        }
    }

    if (!has_semicolon) {
        strncat(def_msg, ";", sizeof(def_msg) - strlen(def_msg) - 1);
    }

    send_to_channel(def_msg);
    free(branch_targets);
}

void initDictionary(Env *env) {
    addWord(&env->dictionary, ".S", OP_DOT_S, 0);
    addWord(&env->dictionary, ".", OP_DOT, 0);
    addWord(&env->dictionary, "+", OP_ADD, 0);
    addWord(&env->dictionary, "-", OP_SUB, 0);
    addWord(&env->dictionary, "*", OP_MUL, 0);
    addWord(&env->dictionary, "/", OP_DIV, 0);
    addWord(&env->dictionary, "MOD", OP_MOD, 0);
    addWord(&env->dictionary, "DUP", OP_DUP, 0);
    addWord(&env->dictionary, "DROP", OP_DROP, 0);
    addWord(&env->dictionary, "SWAP", OP_SWAP, 0);
    addWord(&env->dictionary, "OVER", OP_OVER, 0);
    addWord(&env->dictionary, "ROT", OP_ROT, 0);
    addWord(&env->dictionary, ">R", OP_TO_R, 0);
    addWord(&env->dictionary, "R>", OP_FROM_R, 0);
    addWord(&env->dictionary, "R@", OP_R_FETCH, 0);
    addWord(&env->dictionary, "=", OP_EQ, 0);
    addWord(&env->dictionary, "<", OP_LT, 0);
    addWord(&env->dictionary, ">", OP_GT, 0);
    addWord(&env->dictionary, "AND", OP_AND, 0);
    addWord(&env->dictionary, "OR", OP_OR, 0);
    addWord(&env->dictionary, "NOT", OP_NOT, 0);
    addWord(&env->dictionary, "XOR", OP_XOR, 0);
    addWord(&env->dictionary, "&", OP_BIT_AND, 0);
    addWord(&env->dictionary, "|", OP_BIT_OR, 0);
    addWord(&env->dictionary, "^", OP_BIT_XOR, 0);
    addWord(&env->dictionary, "~", OP_BIT_NOT, 0);
    addWord(&env->dictionary, "<<", OP_LSHIFT, 0);
    addWord(&env->dictionary, ">>", OP_RSHIFT, 0);
    addWord(&env->dictionary, "CR", OP_CR, 0);
    addWord(&env->dictionary, "EMIT", OP_EMIT, 0);
    addWord(&env->dictionary, "VARIABLE", OP_VARIABLE, 0);
    addWord(&env->dictionary, "@", OP_FETCH, 0);
    addWord(&env->dictionary, "!", OP_STORE, 0);
    addWord(&env->dictionary, "+!", OP_PLUSSTORE, 0);
    addWord(&env->dictionary, "DO", OP_DO, 0);
    addWord(&env->dictionary, "LOOP", OP_LOOP, 0);
    addWord(&env->dictionary, "I", OP_I, 0);
    addWord(&env->dictionary, "WORDS", OP_WORDS, 0);
    addWord(&env->dictionary, "LOAD", OP_LOAD, 0);
    addWord(&env->dictionary, "CREATE", OP_CREATE, 0);
    addWord(&env->dictionary, "ALLOT", OP_ALLOT, 0);
    addWord(&env->dictionary, ".\"", OP_DOT_QUOTE, 0);
    addWord(&env->dictionary, "CLOCK", OP_CLOCK, 0);
    addWord(&env->dictionary, "BEGIN", OP_BEGIN, 0);
    addWord(&env->dictionary, "WHILE", OP_WHILE, 0);
    addWord(&env->dictionary, "REPEAT", OP_REPEAT, 0);
    addWord(&env->dictionary, "AGAIN", OP_AGAIN, 0);
    // addWord(&env->dictionary, "RECURSE", OP_CALL, 1);
    addWord(&env->dictionary, "SQRT", OP_SQRT, 0);
    addWord(&env->dictionary, "UNLOOP", OP_UNLOOP, 0);
    addWord(&env->dictionary, "+LOOP", OP_PLUS_LOOP, 0);
    addWord(&env->dictionary, "PICK", OP_PICK, 0);
    addWord(&env->dictionary, "CLEAR-STACK", OP_CLEAR_STACK, 0);
    addWord(&env->dictionary, "PRINT", OP_PRINT, 0);
    addWord(&env->dictionary, "NUM-TO-BIN", OP_NUM_TO_BIN, 0);
    addWord(&env->dictionary, "PRIME?", OP_PRIME_TEST, 0);
    addWord(&env->dictionary, "FORGET", OP_FORGET, 1);
    addWord(&env->dictionary, "STRING", OP_STRING, 1);
    addWord(&env->dictionary, "\"", OP_QUOTE, 0);
    addWord(&env->dictionary, "2DROP", OP_2DROP, 0);
    addWord(&env->dictionary, "IMAGE", OP_IMAGE, 0);
    addWord(&env->dictionary, "TEMP-IMAGE", OP_TEMP_IMAGE, 0);
    addWord(&env->dictionary, "CLEAR-STRINGS", OP_CLEAR_STRINGS, 0);
    addWord(&env->dictionary, "DELAY", OP_DELAY, 0);
    addWord(&env->dictionary, "EXIT", OP_EXIT, 0);
    addWord(&env->dictionary, "MICRO", OP_MICRO, 0);
    addWord(&env->dictionary, "MILLI", OP_MILLI, 0);
}
