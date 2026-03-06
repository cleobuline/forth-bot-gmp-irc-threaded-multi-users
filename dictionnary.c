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


/* ─────────────────────────────────────────────────────────────────────────────
 * clearCompiledWord
 *
 * Libère tous les buffers dynamiques d'un slot CompiledWord et remet tout à
 * zéro, sans toucher au slot lui-même (le slot reste utilisable pour un
 * nouveau mot).  À appeler AVANT de réaffecter un slot existant, que ce soit
 * lors d'une redéfinition (":") ou dans addWord().
 * ───────────────────────────────────────────────────────────────────────────*/
void clearCompiledWord(CompiledWord *word) {
    if (!word) return;
    if (word->name) {
        free(word->name);
        word->name = NULL;
    }
    if (word->strings) {
        for (int i = 0; i < word->string_count; i++) {
            if (word->strings[i]) free(word->strings[i]);
        }
        free(word->strings);
        word->strings = NULL;
    }
    if (word->code) {
        free(word->code);
        word->code = NULL;
    }
    word->code_length     = 0;
    word->code_capacity   = 0;
    word->string_count    = 0;
    word->string_capacity = 0;
    word->immediate       = 0;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * Hash table
 * ───────────────────────────────────────────────────────────────────────────*/

void initWordHash(Env *env) {
    env->word_hash = NULL;
}

void addWordToHash(Env *env, const char *name, int index) {
    WordHash *entry = (WordHash *)SAFE_MALLOC(sizeof(WordHash));
    entry->name = strdup(name);
    if (!entry->name) {
        free(entry);
        send_to_channel("Erreur : Échec de l'allocation pour le nom dans WordHash");
        return;
    }
    entry->index = index;
    HASH_ADD_STR(env->word_hash, name, entry);
}

int findCompiledWordIndex(char *name, Env *env) {
    if (!env || !name) return -1;
    WordHash *entry;
    HASH_FIND_STR(env->word_hash, name, entry);
    return entry ? entry->index : -1;
}

void removeWordFromHash(Env *env, const char *name) {
    WordHash *entry;
    HASH_FIND_STR(env->word_hash, name, entry);
    if (entry) {
        HASH_DEL(env->word_hash, entry);
        free(entry->name);
        free(entry);
    }
}

void freeWordHash(Env *env) {
    WordHash *entry, *tmp;
    HASH_ITER(hh, env->word_hash, entry, tmp) {
        HASH_DEL(env->word_hash, entry);
        free(entry->name);
        free(entry);
    }
    env->word_hash = NULL;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * freeCurrentWord  (mot en cours de compilation)
 * ───────────────────────────────────────────────────────────────────────────*/
void freeCurrentWord(Env *env) {
    clearCompiledWord(&env->currentWord);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * initDynamicDictionary
 *
 * CORRECTION point 7 : on n'alloue PLUS code[] ni strings[] pour les slots
 * vides.  calloc() met tout à zéro (NULL, 0), ce qui est l'état "non utilisé"
 * attendu.  Les allocations réelles se font dans addWord() / compileToken()
 * au moment où le slot est vraiment rempli.
 * ───────────────────────────────────────────────────────────────────────────*/
void initDynamicDictionary(DynamicDictionary *dict) {
    dict->capacity = 128;
    dict->count    = 0;
    /* calloc initialise tout à 0/NULL : name=NULL, code=NULL, strings=NULL,
       *_capacity=0, *_length=0, immediate=0.  Rien d'autre à faire. */
    dict->words = (CompiledWord *)calloc(dict->capacity, sizeof(CompiledWord));
    if (!dict->words) {
        send_to_channel("Erreur : Échec de l'allocation du dictionnaire");
        exit(1);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * resizeCompiledWordArrays  (appelé depuis compiletoken.c)
 * ───────────────────────────────────────────────────────────────────────────*/
void resizeCompiledWordArrays(CompiledWord *word, int is_code) {
    if (is_code) {
        long int new_capacity = word->code_capacity ? word->code_capacity * 2 : 1;
        Instruction *new_code = (Instruction *)SAFE_REALLOC(word->code,
                                    new_capacity * sizeof(Instruction));
        memset(new_code + word->code_capacity, 0,
               (new_capacity - word->code_capacity) * sizeof(Instruction));
        word->code          = new_code;
        word->code_capacity = new_capacity;
    } else {
        long int new_capacity = word->string_capacity ? word->string_capacity * 2 : 1;
        char **new_strings = (char **)SAFE_REALLOC(word->strings,
                                    new_capacity * sizeof(char *));
        memset(new_strings + word->string_capacity, 0,
               (new_capacity - word->string_capacity) * sizeof(char *));
        word->strings          = new_strings;
        word->string_capacity  = new_capacity;
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * resizeDynamicDictionary
 *
 * CORRECTION point 7 : les nouveaux slots sont mis à zéro avec memset(),
 * sans pré-allouer code[] ni strings[].
 * ───────────────────────────────────────────────────────────────────────────*/
void resizeDynamicDictionary(DynamicDictionary *dict) {
    long int new_capacity = dict->capacity * 2;
    CompiledWord *new_words = (CompiledWord *)SAFE_REALLOC(dict->words,
                                  new_capacity * sizeof(CompiledWord));
    if (!new_words) {
        send_to_channel("Error: Failed to resize dictionary, try to FORGET some WORDS");
        return;
    }

    dict->words = new_words;

    /* Mettre à zéro uniquement les nouveaux slots (NULL/0 = état "vide") */
    memset(&dict->words[dict->capacity], 0,
           (new_capacity - dict->capacity) * sizeof(CompiledWord));

    dict->capacity = new_capacity;
}


/* ─────────────────────────────────────────────────────────────────────────────
 * addWord  — ajoute un mot built-in au dictionnaire
 *
 * CORRECTION point 7 : on appelle clearCompiledWord() avant de remplir le
 * slot afin de libérer les éventuels buffers pré-alloués (il n'y en a plus
 * avec la nouvelle initDynamicDictionary, mais la défense en profondeur ne
 * coûte rien).
 * ───────────────────────────────────────────────────────────────────────────*/
void addWord(DynamicDictionary *dict, const char *name, OpCode opcode,
             int immediate, Env *env) {
    if (dict->count >= dict->capacity) {
        resizeDynamicDictionary(dict);
    }
    if (dict->count >= dict->capacity) {
        send_to_channel("addWord: dictionary is full, cannot add more words");
        return;
    }

    CompiledWord *word = &dict->words[dict->count];

    /* Libérer d'éventuels résidus (défense en profondeur) */
    clearCompiledWord(word);

    word->name = strdup(name);
    if (!word->name) {
        send_to_channel("Erreur : Échec strdup dans addWord");
        return;
    }

    /* Un seul slot d'instruction suffit pour un mot built-in */
    word->code = (Instruction *)SAFE_MALLOC(sizeof(Instruction));
    word->code_capacity   = 1;
    word->code[0].opcode  = opcode;
    word->code[0].operand = 0;
    word->code_length     = 1;

    /* Pas de strings pour les mots built-in */
    word->strings          = NULL;
    word->string_capacity  = 0;
    word->string_count     = 0;
    word->immediate        = immediate;

    addWordToHash(env, name, dict->count);
    dict->count++;
}

 

/* ─────────────────────────────────────────────────────────────────────────────
 * print_word_definition_irc  (SEE)
 * ───────────────────────────────────────────────────────────────────────────*/
void print_word_definition_irc(int index, Stack *stack __attribute__((unused)),
                                Env *env) {
    if (!env || index < 0 || index >= env->dictionary.count) {
        send_to_channel("SEE: Unknown word");
        return;
    }

    CompiledWord *word = &env->dictionary.words[index];
    char def_msg[1024] = "";
    snprintf(def_msg, sizeof(def_msg), ": %s ", word->name);

    typedef struct {
        long int if_addr;
        long int else_addr;
        long int then_addr;
    } IfBlock;

    IfBlock if_blocks[64];
    int if_top        = 0;
    int has_semicolon = 0;

    for (long int i = 0; i < word->code_length; i++) {
        Instruction instr    = word->code[i];
        char        instr_str[128] = "";

        /* Vérifier si cette position est une cible THEN */
        for (int j = 0; j < if_top; j++) {
            if (if_blocks[j].then_addr == i ||
                (if_blocks[j].else_addr != -1 &&
                 word->code[if_blocks[j].else_addr].operand == i)) {
                if (j == if_top - 1) if_top--;
                if (strlen(def_msg) + 5 >= sizeof(def_msg) - 1) {
                    send_to_channel(def_msg);
                    snprintf(def_msg, sizeof(def_msg), "THEN ");
                } else {
                    strncat(def_msg, "THEN ", sizeof(def_msg) - strlen(def_msg) - 1);
                }
                break;
            }
        }

        switch (instr.opcode) {
        case OP_PUSH: {
            if (instr.operand & TYPE_MASK) {
                MemoryNode *node = memory_get(&env->memory_list, instr.operand);
                if (node) {
                    if (node->type == TYPE_CONSTANT) {
                        char *s = mpz_get_str(NULL, 10, node->value.number);
                        snprintf(instr_str, sizeof(instr_str), "%s ", s ? s : "<err>");
                        if (s) free(s);
                    } else if (node->type == TYPE_VAR) {
                        char *s = mpz_get_str(NULL, 10, node->value.number);
                        snprintf(instr_str, sizeof(instr_str),
                                 "%s ( variable = %s ) ", node->name, s ? s : "?");
                        if (s) free(s);
                    } else if (node->type == TYPE_STRING) {
                        snprintf(instr_str, sizeof(instr_str),
                                 "%s ( string = \"%s\" ) ", node->name,
                                 node->value.string ? node->value.string : "<empty>");
                    } else if (node->type == TYPE_ARRAY) {
                        snprintf(instr_str, sizeof(instr_str),
                                 "%s ( array size=%lu ) ",
                                 node->name, node->value.array.size);
                    } else {
                        snprintf(instr_str, sizeof(instr_str),
                                 "<type%lu:%lx> ", node->type, instr.operand);
                    }
                } else {
                    snprintf(instr_str, sizeof(instr_str),
                             "<bad-ref:%lx> ", instr.operand);
                }
            } else {
                /* instr.operand est l'INDEX dans word->strings[], pas la valeur.
                 * On affiche la vraie valeur textuelle stockée dans strings[]. */
                if (instr.operand >= 0 && instr.operand < word->string_count
                    && word->strings[instr.operand]) {
                    snprintf(instr_str, sizeof(instr_str), "%s ",
                             word->strings[instr.operand]);
                } else {
                    /* fallback : built-in avec operand direct (ne devrait pas
                     * passer par SEE, mais au cas où) */
                    snprintf(instr_str, sizeof(instr_str), "%ld ", instr.operand);
                }
            }
            break;
        }
        case OP_CALL:
            if (instr.operand < env->dictionary.count &&
                env->dictionary.words[instr.operand].name)
                snprintf(instr_str, sizeof(instr_str), "%s ",
                         env->dictionary.words[instr.operand].name);
            else
                snprintf(instr_str, sizeof(instr_str), "(CALL %ld) ", instr.operand);
            break;
        case OP_DUP:       snprintf(instr_str, sizeof(instr_str), "DUP ");    break;
        case OP_DROP:      snprintf(instr_str, sizeof(instr_str), "DROP ");   break;
        case OP_SWAP:      snprintf(instr_str, sizeof(instr_str), "SWAP ");   break;
        case OP_OVER:      snprintf(instr_str, sizeof(instr_str), "OVER ");   break;
        case OP_ROT:       snprintf(instr_str, sizeof(instr_str), "ROT ");    break;
        case OP_ADD:       snprintf(instr_str, sizeof(instr_str), "+ ");      break;
        case OP_SUB:       snprintf(instr_str, sizeof(instr_str), "- ");      break;
        case OP_MUL:       snprintf(instr_str, sizeof(instr_str), "* ");      break;
        case OP_DIV:       snprintf(instr_str, sizeof(instr_str), "/ ");      break;
        case OP_MOD:       snprintf(instr_str, sizeof(instr_str), "MOD ");    break;
        case OP_EQ:        snprintf(instr_str, sizeof(instr_str), "= ");      break;
        case OP_LT:        snprintf(instr_str, sizeof(instr_str), "< ");      break;
        case OP_GT:        snprintf(instr_str, sizeof(instr_str), "> ");      break;
        case OP_AND:       snprintf(instr_str, sizeof(instr_str), "AND ");    break;
        case OP_OR:        snprintf(instr_str, sizeof(instr_str), "OR ");     break;
        case OP_NOT:       snprintf(instr_str, sizeof(instr_str), "NOT ");    break;
        case OP_XOR:       snprintf(instr_str, sizeof(instr_str), "XOR ");    break;
        case OP_BIT_AND:   snprintf(instr_str, sizeof(instr_str), "& ");      break;
        case OP_BIT_OR:    snprintf(instr_str, sizeof(instr_str), "| ");      break;
        case OP_BIT_XOR:   snprintf(instr_str, sizeof(instr_str), "^ ");      break;
        case OP_BIT_NOT:   snprintf(instr_str, sizeof(instr_str), "~ ");      break;
        case OP_LSHIFT:    snprintf(instr_str, sizeof(instr_str), "<< ");     break;
        case OP_RSHIFT:    snprintf(instr_str, sizeof(instr_str), ">> ");     break;
        case OP_DOT:       snprintf(instr_str, sizeof(instr_str), ". ");      break;
        case OP_DOT_S:     snprintf(instr_str, sizeof(instr_str), ".S ");     break;
        case OP_CR:        snprintf(instr_str, sizeof(instr_str), "CR ");     break;
        case OP_EMIT:      snprintf(instr_str, sizeof(instr_str), "EMIT ");   break;
        case OP_FETCH:     snprintf(instr_str, sizeof(instr_str), "@ ");      break;
        case OP_STORE:     snprintf(instr_str, sizeof(instr_str), "! ");      break;
        case OP_PLUSSTORE: snprintf(instr_str, sizeof(instr_str), "+! ");     break;
        case OP_TO_R:      snprintf(instr_str, sizeof(instr_str), ">R ");     break;
        case OP_FROM_R:    snprintf(instr_str, sizeof(instr_str), "R> ");     break;
        case OP_R_FETCH:   snprintf(instr_str, sizeof(instr_str), "R@ ");     break;
        case OP_I:         snprintf(instr_str, sizeof(instr_str), "I ");      break;
        case OP_J:         snprintf(instr_str, sizeof(instr_str), "J ");      break;
        case OP_R_FETCH_UL: snprintf(instr_str, sizeof(instr_str), "R@UL "); break;
        case OP_R_STORE_UL: snprintf(instr_str, sizeof(instr_str), "R!UL "); break;
        case OP_BRANCH_FALSE:
            snprintf(instr_str, sizeof(instr_str), "IF ");
            if (if_top < 64) {
                if_blocks[if_top].if_addr   = i;
                if_blocks[if_top].else_addr = -1;
                if_blocks[if_top].then_addr = word->code[i].operand;
                if_top++;
            }
            break;
        case OP_BRANCH:
            if (if_top > 0 && if_blocks[if_top - 1].else_addr == -1) {
                snprintf(instr_str, sizeof(instr_str), "ELSE ");
                if_blocks[if_top - 1].else_addr = i;
                if_blocks[if_top - 1].then_addr = word->code[i].operand;
            } else {
                snprintf(instr_str, sizeof(instr_str),
                         "BRANCH(%ld) ", word->code[i].operand);
            }
            break;
        case OP_END:
            if (i + 1 == word->code_length) {
                snprintf(instr_str, sizeof(instr_str), "; ");
                has_semicolon = 1;
            } else {
                snprintf(instr_str, sizeof(instr_str), "END ");
            }
            break;
        case OP_DO:        snprintf(instr_str, sizeof(instr_str), "DO ");     break;
        case OP_LOOP:      snprintf(instr_str, sizeof(instr_str), "LOOP ");   break;
        case OP_PLUS_LOOP: snprintf(instr_str, sizeof(instr_str), "+LOOP ");  break;
        case OP_UNLOOP:    snprintf(instr_str, sizeof(instr_str), "UNLOOP "); break;
        case OP_BEGIN:     snprintf(instr_str, sizeof(instr_str), "BEGIN ");  break;
        case OP_WHILE:     snprintf(instr_str, sizeof(instr_str), "WHILE ");  break;
        case OP_REPEAT:    snprintf(instr_str, sizeof(instr_str), "REPEAT "); break;
        case OP_UNTIL:     snprintf(instr_str, sizeof(instr_str), "UNTIL ");  break;
        case OP_AGAIN:     snprintf(instr_str, sizeof(instr_str), "AGAIN ");  break;
        case OP_CASE:      snprintf(instr_str, sizeof(instr_str), "CASE ");   break;
        case OP_OF:        snprintf(instr_str, sizeof(instr_str), "OF ");     break;
        case OP_ENDOF:     snprintf(instr_str, sizeof(instr_str), "ENDOF ");  break;
        case OP_ENDCASE:   snprintf(instr_str, sizeof(instr_str), "ENDCASE "); break;
        case OP_DOT_QUOTE:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         ".\" %s \" ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), ".\"(invalid) ");
            break;
        case OP_QUOTE:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         "\" %s \" ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), "\"(invalid) ");
            break;
        case OP_VARIABLE:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         "VARIABLE %s ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), "VARIABLE(invalid) ");
            break;
        case OP_STRING:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         "STRING %s ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), "STRING(invalid) ");
            break;
        case OP_FORGET:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         "FORGET %s ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), "FORGET(invalid) ");
            break;
        case OP_CREATE:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         "CREATE %s ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), "CREATE(invalid) ");
            break;
        case OP_ALLOT:     snprintf(instr_str, sizeof(instr_str), "ALLOT ");      break;
        case OP_LOAD:
            if (instr.operand < word->string_count && word->strings[instr.operand])
                snprintf(instr_str, sizeof(instr_str),
                         "LOAD %s ", word->strings[instr.operand]);
            else
                snprintf(instr_str, sizeof(instr_str), "LOAD(invalid) ");
            break;
        case OP_WORDS:       snprintf(instr_str, sizeof(instr_str), "WORDS ");       break;
        case OP_NUM_TO_BIN:  snprintf(instr_str, sizeof(instr_str), "NUM-TO-BIN ");  break;
        case OP_PRIME_TEST:  snprintf(instr_str, sizeof(instr_str), "PRIME? ");      break;
        case OP_RECURSE:     snprintf(instr_str, sizeof(instr_str), "RECURSE ");     break;
        case OP_SQRT:        snprintf(instr_str, sizeof(instr_str), "SQRT ");        break;
        case OP_PICK:        snprintf(instr_str, sizeof(instr_str), "PICK ");        break;
        case OP_ROLL:        snprintf(instr_str, sizeof(instr_str), "ROLL ");        break;
        case OP_DEPTH:       snprintf(instr_str, sizeof(instr_str), "DEPTH ");       break;
        case OP_TOP:         snprintf(instr_str, sizeof(instr_str), "TOP ");         break;
        case OP_NIP:         snprintf(instr_str, sizeof(instr_str), "NIP ");         break;
        case OP_CLEAR_STACK: snprintf(instr_str, sizeof(instr_str), "CLEAR-STACK "); break;
        case OP_CLOCK:       snprintf(instr_str, sizeof(instr_str), "CLOCK ");       break;
        case OP_SEE:         snprintf(instr_str, sizeof(instr_str), "SEE ");         break;
        case OP_2DROP:       snprintf(instr_str, sizeof(instr_str), "2DROP ");       break;
        case OP_CONSTANT:    snprintf(instr_str, sizeof(instr_str), "%ld ", instr.operand); break;
        case OP_MICRO:       snprintf(instr_str, sizeof(instr_str), "MICRO ");       break;
        case OP_MILLI:       snprintf(instr_str, sizeof(instr_str), "MILLI ");       break;
        case OP_EXIT:        snprintf(instr_str, sizeof(instr_str), "EXIT ");        break;
        case OP_DELAY:       snprintf(instr_str, sizeof(instr_str), "DELAY ");       break;
        case OP_IMAGE:       snprintf(instr_str, sizeof(instr_str), "IMAGE ");       break;
        case OP_TEMP_IMAGE:  snprintf(instr_str, sizeof(instr_str), "TEMP-IMAGE ");  break;
        case OP_CLEAR_STRINGS: snprintf(instr_str, sizeof(instr_str), "CLEAR-STRINGS "); break;
        case OP_PRINT:       snprintf(instr_str, sizeof(instr_str), "PRINT ");       break;
        case OP_MOON_PHASE:  snprintf(instr_str, sizeof(instr_str), "MOON-PHASE ");  break;
        case OP_QUESTION_DO: snprintf(instr_str, sizeof(instr_str), "?DO ");         break;
        case OP_APPEND:      snprintf(instr_str, sizeof(instr_str), "APPEND ");      break;
        default:
            snprintf(instr_str, sizeof(instr_str),
                     "(OP_%d %ld) ", instr.opcode, instr.operand);
            break;
        }

        if (instr_str[0] != '\0') {
            if (strlen(def_msg) + strlen(instr_str) >= sizeof(def_msg) - 1) {
                send_to_channel(def_msg);
                snprintf(def_msg, sizeof(def_msg), "%s ", instr_str);
            } else {
                strncat(def_msg, instr_str,
                        sizeof(def_msg) - strlen(def_msg) - 1);
            }
        }
    }

    if (!has_semicolon)
        strncat(def_msg, ";", sizeof(def_msg) - strlen(def_msg) - 1);

    send_to_channel(def_msg);
}


/* ─────────────────────────────────────────────────────────────────────────────
 * initDictionary  — charge tous les mots built-in
 * ───────────────────────────────────────────────────────────────────────────*/
void initDictionary(Env *env) {
    addWord(&env->dictionary, ".S",          OP_DOT_S,        0, env);
    addWord(&env->dictionary, ".",           OP_DOT,          0, env);
    addWord(&env->dictionary, "+",           OP_ADD,          0, env);
    addWord(&env->dictionary, "-",           OP_SUB,          0, env);
    addWord(&env->dictionary, "*",           OP_MUL,          0, env);
    addWord(&env->dictionary, "/",           OP_DIV,          0, env);
    addWord(&env->dictionary, "MOD",         OP_MOD,          0, env);
    addWord(&env->dictionary, "DUP",         OP_DUP,          0, env);
    addWord(&env->dictionary, "DROP",        OP_DROP,         0, env);
    addWord(&env->dictionary, "SWAP",        OP_SWAP,         0, env);
    addWord(&env->dictionary, "OVER",        OP_OVER,         0, env);
    addWord(&env->dictionary, "ROT",         OP_ROT,          0, env);
    addWord(&env->dictionary, ">R",          OP_TO_R,         0, env);
    addWord(&env->dictionary, "R>",          OP_FROM_R,       0, env);
    addWord(&env->dictionary, "R@",          OP_R_FETCH,      0, env);
    addWord(&env->dictionary, "R@UL",        OP_R_FETCH_UL,   0, env);
    addWord(&env->dictionary, "R!UL",        OP_R_STORE_UL,   0, env);
    addWord(&env->dictionary, "=",           OP_EQ,           0, env);
    addWord(&env->dictionary, "<",           OP_LT,           0, env);
    addWord(&env->dictionary, ">",           OP_GT,           0, env);
    addWord(&env->dictionary, "AND",         OP_AND,          0, env);
    addWord(&env->dictionary, "OR",          OP_OR,           0, env);
    addWord(&env->dictionary, "NOT",         OP_NOT,          0, env);
    addWord(&env->dictionary, "XOR",         OP_XOR,          0, env);
    addWord(&env->dictionary, "&",           OP_BIT_AND,      0, env);
    addWord(&env->dictionary, "|",           OP_BIT_OR,       0, env);
    addWord(&env->dictionary, "^",           OP_BIT_XOR,      0, env);
    addWord(&env->dictionary, "~",           OP_BIT_NOT,      0, env);
    addWord(&env->dictionary, "<<",          OP_LSHIFT,       0, env);
    addWord(&env->dictionary, ">>",          OP_RSHIFT,       0, env);
    addWord(&env->dictionary, "CR",          OP_CR,           0, env);
    addWord(&env->dictionary, "EMIT",        OP_EMIT,         0, env);
    addWord(&env->dictionary, "VARIABLE",    OP_VARIABLE,     0, env);
    addWord(&env->dictionary, "@",           OP_FETCH,        0, env);
    addWord(&env->dictionary, "!",           OP_STORE,        0, env);
    addWord(&env->dictionary, "+!",          OP_PLUSSTORE,    0, env);
    addWord(&env->dictionary, "DO",          OP_DO,           0, env);
    addWord(&env->dictionary, "LOOP",        OP_LOOP,         0, env);
    addWord(&env->dictionary, "I",           OP_I,            0, env);
    addWord(&env->dictionary, "WORDS",       OP_WORDS,        0, env);
    addWord(&env->dictionary, "LOAD",        OP_LOAD,         0, env);
    addWord(&env->dictionary, "CREATE",      OP_CREATE,       0, env);
    addWord(&env->dictionary, "ALLOT",       OP_ALLOT,        0, env);
    addWord(&env->dictionary, ".\"",         OP_DOT_QUOTE,    0, env);
    addWord(&env->dictionary, "CLOCK",       OP_CLOCK,        0, env);
    addWord(&env->dictionary, "BEGIN",       OP_BEGIN,        0, env);
    addWord(&env->dictionary, "WHILE",       OP_WHILE,        0, env);
    addWord(&env->dictionary, "REPEAT",      OP_REPEAT,       0, env);
    addWord(&env->dictionary, "AGAIN",       OP_AGAIN,        0, env);
    addWord(&env->dictionary, "SQRT",        OP_SQRT,         0, env);
    addWord(&env->dictionary, "UNLOOP",      OP_UNLOOP,       0, env);
    addWord(&env->dictionary, "+LOOP",       OP_PLUS_LOOP,    0, env);
    addWord(&env->dictionary, "PICK",        OP_PICK,         0, env);
    addWord(&env->dictionary, "CLEAR-STACK", OP_CLEAR_STACK,  0, env);
    addWord(&env->dictionary, "PRINT",       OP_PRINT,        0, env);
    addWord(&env->dictionary, "NUM-TO-BIN",  OP_NUM_TO_BIN,   0, env);
    addWord(&env->dictionary, "PRIME?",      OP_PRIME_TEST,   0, env);
    addWord(&env->dictionary, "FORGET",      OP_FORGET,       1, env);
    addWord(&env->dictionary, "STRING",      OP_STRING,       1, env);
    addWord(&env->dictionary, "S\"",         OP_QUOTE,        0, env);
    addWord(&env->dictionary, "\"S",         OP_QUOTE_END,    0, env);
    addWord(&env->dictionary, "2DROP",       OP_2DROP,        0, env);
    addWord(&env->dictionary, "IMAGE",       OP_IMAGE,        0, env);
    addWord(&env->dictionary, "TEMP-IMAGE",  OP_TEMP_IMAGE,   0, env);
    addWord(&env->dictionary, "CLEAR-STRINGS", OP_CLEAR_STRINGS, 0, env);
    addWord(&env->dictionary, "DELAY",       OP_DELAY,        0, env);
    addWord(&env->dictionary, "EXIT",        OP_EXIT,         0, env);
    addWord(&env->dictionary, "CONSTANT",    OP_CONSTANT,     0, env);
    addWord(&env->dictionary, "MICRO",       OP_MICRO,        0, env);
    addWord(&env->dictionary, "MILLI",       OP_MILLI,        0, env);
    addWord(&env->dictionary, "ROLL",        OP_ROLL,         0, env);
    addWord(&env->dictionary, "DEPTH",       OP_DEPTH,        0, env);
    addWord(&env->dictionary, "APPEND",      OP_APPEND,       0, env);
    addWord(&env->dictionary, "MOON-PHASE",  OP_MOON_PHASE,   0, env);
    addWord(&env->dictionary, "?DO",         OP_QUESTION_DO,  0, env);
}
