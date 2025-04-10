#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "memory_forth.h"

void memory_init(MemoryList *list) {
    list->head = NULL;
    list->count = 0;
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
    unsigned long pos = 0;

    while (current) {
        if (strcmp(current->name, name) == 0) {
            // Calculer l'index brut basé sur la position (comme dans memory_get)
            unsigned long index = (list->count - 1 - pos) & INDEX_MASK;
            // Encoder l'index avec le type du nœud
            unsigned long encoded_index = (current->type << 28) | index;
            return encoded_index;
        }
        current = current->next;
        pos++;
    }
    return 0; // Non trouvé
}
unsigned long memory_create(MemoryList *list, const char *name, unsigned long type) {
    MemoryNode *node = (MemoryNode *)SAFE_MALLOC(sizeof(MemoryNode));
    if (!node) return 0;

    node->name = strdup(name);
    node->type = type;
    node->next = list->head;

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
    unsigned long index = list->count & INDEX_MASK;
    // Vérifier si l’index est déjà utilisé
    MemoryNode *current = list->head->next; // Exclure le nœud qu’on vient d’ajouter
    while (current) {
        unsigned long current_index = (list->count - 1 - (list->head - current)) & INDEX_MASK;
        if (current_index == index) {
            index = (list->count + 1) & INDEX_MASK; // Passer à l’index suivant
            list->count++; // Ajuster le compteur
        }
        current = current->next;
    }
    list->count++;
    unsigned long encoded_index = (type << 28) | index;
    return encoded_index;
}
MemoryNode *memory_get(MemoryList *list, unsigned long encoded_index) {
    MemoryNode *current = list->head;
    unsigned long target_index = encoded_index & INDEX_MASK;
    unsigned long pos = 0;
 
    while (current != NULL) {
        unsigned long current_index = (list->count - 1 - pos) & INDEX_MASK;
        /* snprintf(debug_msg, sizeof(debug_msg), "memory_get: Checking pos=%lu, current_index=%lu, type=%lu", pos, current_index, current->type);
        send_to_channel(debug_msg);
        */ 
        if (current_index == target_index) {
            return current;
        }
        current = current->next;
        pos++;
    }
    /* snprintf(debug_msg, sizeof(debug_msg), "memory_get: Not found for index=%lu", target_index);
    send_to_channel(debug_msg);
    */ 
    return NULL;
}
unsigned long memory_get_type(unsigned long encoded_index) {
    return (encoded_index & TYPE_MASK) >> 28;
}

void memory_store(MemoryList *list, unsigned long encoded_index, const void *data) {
    MemoryNode *node = memory_get(list, encoded_index);
    if (!node || node->type != memory_get_type(encoded_index) || !data) {
        return;
    }

    if (node->type == TYPE_VAR) {
        mpz_set(node->value.number, *(mpz_t *)data);
    } else if (node->type == TYPE_STRING) {
        if (node->value.string) free(node->value.string);
        node->value.string = strdup((char *)data);
    } else if (node->type == TYPE_ARRAY) {
        // Pour l'instant, non implémenté pour les tableaux
        // À ajouter si nécessaire (par exemple, stockage à un offset)
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

 
