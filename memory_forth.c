#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "forth_bot.h"
#include "memory_forth.h"
void memory_init(MemoryList *list) {
    list->head          = NULL;
    list->count         = 0;
    list->total_created = 0;  // ← initialiser
}
MemoryNode *memory_get_by_name(MemoryList *list, const char *name) {
    MemoryNode *current = list->head;
    while (current) {
        if (current->name && strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL; // Retourne NULL si le nom n’est pas trouvé
}
unsigned long memory_find_by_name(MemoryList *list, const char *name) {
    MemoryNode *current = list->head;
    while (current) {
        if (strcmp(current->name, name) == 0) return ((unsigned long)current->type << 28) | (current->index & INDEX_MASK);
        current = current->next;
    }
    return 0;
}
unsigned long memory_create(MemoryList *list, const char *name, unsigned long type) {
    MemoryNode *node = (MemoryNode *)SAFE_MALLOC(sizeof(MemoryNode));
    if (!node) return 0;

    node->name = strdup(name);
    if (!node->name) {
        free(node);
        return 0;
    }
    node->type  = type;
    node->index = list->total_created & INDEX_MASK;  // index unique et stable
    node->next  = list->head;

    if (type == TYPE_VAR) {
        mpz_init(node->value.number);
        mpz_set_ui(node->value.number, 0);
    } else if (type == TYPE_STRING) {
        node->value.string = NULL;
    } else if (type == TYPE_ARRAY) {
        node->value.array.data = NULL;
        node->value.array.size = 0;
    }

    list->head = node;
    list->total_created++;
    list->count++;

    return (type << 28) | node->index;
}
MemoryNode *memory_get(MemoryList *list, unsigned long encoded_index) {
    if (!list || !list->head) {
        send_to_channel("DEBUG: memory_get - list vide ou NULL");
        return NULL;
    }

    unsigned long target_type  = (encoded_index & TYPE_MASK) >> 28;
    unsigned long target_index = encoded_index & INDEX_MASK;

     
 

    MemoryNode *current = list->head;
    int pos = 0;
    while (current) {
   

        if (current->index == target_index && current->type == target_type) {
            // send_to_channel("DEBUG: memory_get - MATCH trouvé !");
            return current;
        }
        current = current->next;
        pos++;
    }

    //send_to_channel("DEBUG: memory_get - AUCUN match trouvé");
    return NULL;
}
unsigned long memory_get_type(unsigned long encoded_index) {
    return (encoded_index & TYPE_MASK) >> 28;
}

void memory_store(MemoryList *list, unsigned long encoded_index, const void *data, Env *env) {
    if (!list || !data) {
        if (env) set_error(env, "memory_store: paramètre invalide (list ou data NULL)");
        send_to_channel("DEBUG: memory_store - paramètre invalide");
        return;
    }

    MemoryNode *node = memory_get(list, encoded_index);
    if (!node) {
        send_to_channel("DEBUG: memory_store - node NON trouvé via memory_get");
        if (env) set_error(env, "memory_store: index mémoire invalide");
        return;
    }

    //char dbg[512];
    //snprintf(dbg, sizeof(dbg), "DEBUG: memory_store - node trouvé : name=%s type=%lu",
      //       node->name ? node->name : "(null)", node->type);
    //send_to_channel(dbg);

    unsigned long node_type = node->type;

    if (node_type == TYPE_VAR || node_type == TYPE_CONSTANT) {
        const mpz_t *src = (const mpz_t *)data;
        mpz_set(node->value.number, *src);

        char *val_str = mpz_get_str(NULL, 10, node->value.number);
       // snprintf(dbg, sizeof(dbg), "DEBUG: memory_store - valeur stockée : %s",
        //         val_str ? val_str : "<mpz_get_str failed>");
       // send_to_channel(dbg);
        if (val_str) free(val_str);
    }
    else if (node_type == TYPE_STRING) {
        const char *src_str = (const char *)data;
        if (node->value.string) free(node->value.string);
        node->value.string = src_str ? strdup(src_str) : NULL;
        //snprintf(dbg, sizeof(dbg), "DEBUG: memory_store - string stockée : %s",
        //         node->value.string ? node->value.string : "(null)");
        // send_to_channel(dbg);
    }
    else {
        //snprintf(dbg, sizeof(dbg), "DEBUG: memory_store - type non supporté : %lu", node_type);
        //send_to_channel(dbg);
        if (env) {
            char err[128];
            snprintf(err, sizeof(err), "memory_store: type mémoire non supporté %lu", node_type);
            set_error(env, err);
        }
    }
}

void memory_fetch(MemoryList *list, unsigned long encoded_index, void *result) {
    MemoryNode *node = memory_get(list, encoded_index);
    if (!node || node->type != memory_get_type(encoded_index) || !result) {
        return;
    }

    if (node->type == TYPE_VAR) {
        mpz_set(*(mpz_t *)result, node->value.number);
    } else if (node->type == TYPE_STRING) {
        char **str_result = (char **)result;
        *str_result = node->value.string ? strdup(node->value.string) : NULL;
    } else if (node->type == TYPE_ARRAY) {
        // Pour l'instant, non implémenté pour les tableaux
        // À ajouter si nécessaire
    }
}

void memory_free(MemoryList *list, const char *name) {
    MemoryNode *prev = NULL, *node = list->head;

    while (node && strcmp(node->name, name) != 0) {
        prev = node;
        node = node->next;
    }
    if (!node) return;

    if (prev) prev->next = node->next;
    else list->head = node->next;

    if (node->type == TYPE_VAR) {
        mpz_clear(node->value.number);
    } else if (node->type == TYPE_STRING && node->value.string) {
        free(node->value.string);
    } else if (node->type == TYPE_ARRAY && node->value.array.data) {
        for (unsigned long i = 0; i < node->value.array.size; i++) {
            mpz_clear(node->value.array.data[i]);
        }
        free(node->value.array.data);
    }
    free(node->name);
    free(node);
    list->count--;
}

void print_variable(MemoryList *list, const char *name) {
    MemoryNode *node = list->head;
    while (node && strcmp(node->name, name) != 0) node = node->next;

    if (!node) {
        printf("Variable '%s' non trouvée\n", name);
        return;
    }
    if (node->type == TYPE_VAR) {
        char *num_str = mpz_get_str(NULL, 10, node->value.number);
        printf("VAR '%s' = %s\n", name, num_str);
        free(num_str);
    } else {
        printf("'%s' n'est pas une variable\n", name);
    }
}

void print_string(MemoryList *list, const char *name) {
    MemoryNode *node = list->head;
    while (node && strcmp(node->name, name) != 0) node = node->next;

    if (!node) {
        printf("String '%s' non trouvée\n", name);
        return;
    }
    if (node->type == TYPE_STRING) {
        printf("STRING '%s' = '%s'\n", name, node->value.string ? node->value.string : "(null)");
    } else {
        printf("'%s' n'est pas une string\n", name);
    }
}

void print_array(MemoryList *list, const char *name) {
    MemoryNode *node = list->head;
    while (node && strcmp(node->name, name) != 0) node = node->next;

    if (!node) {
        printf("Array '%s' non trouvé\n", name);
        return;
    }
    if (node->type == TYPE_ARRAY) {
        printf("ARRAY '%s' (taille = %lu): [", name, node->value.array.size);
        for (unsigned long i = 0; i < node->value.array.size; i++) {
            char *num_str = mpz_get_str(NULL, 10, node->value.array.data[i]);
            printf("%s", num_str);
            free(num_str);
            if (i < node->value.array.size - 1) printf(", ");
        }
        printf("]\n");
    } else {
        printf("'%s' n'est pas un array\n", name);
    }
}

 
