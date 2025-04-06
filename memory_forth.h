#ifndef MEMORY_FORTH_H
#define MEMORY_FORTH_H

#include <gmp.h>
#include <string.h>
#include <stdlib.h>

// Définitions des types avec des valeurs explicites
#define TYPE_VAR    0x1UL  // Variable
#define TYPE_STRING 0x2UL  // Chaîne
#define TYPE_ARRAY  0x3UL  // Tableau

// Masques pour l'encodage de l'index
#define TYPE_MASK   0xF0000000UL  // 4 bits supérieurs pour le type
#define INDEX_MASK  0x0FFFFFFFUL  // 28 bits inférieurs pour l'index

// Structure pour un nœud de mémoire
typedef struct MemoryNode {
    char *name;              // Nom du nœud
    unsigned long type;      // Type (TYPE_VAR, TYPE_STRING, TYPE_ARRAY)
    union {
        mpz_t number;        // Pour TYPE_VAR
        char *string;        // Pour TYPE_STRING
        struct {
            mpz_t *data;     // Données pour TYPE_ARRAY
            unsigned long size; // Taille du tableau
        } array;
    } value;
    struct MemoryNode *next; // Pointeur vers le nœud suivant
} MemoryNode;

// Structure pour la liste de mémoire
typedef struct {
    MemoryNode *head;        // Tête de la liste
    unsigned long count;     // Nombre total de nœuds
} MemoryList;

// Prototypes des fonctions
MemoryNode *memory_get_by_name(MemoryList *list, const char *name); // Nouvelle fonction
void memory_init(MemoryList *list);
unsigned long memory_create(MemoryList *list, const char *name, unsigned long type);
MemoryNode *memory_get(MemoryList *list, unsigned long encoded_index);
void memory_store(MemoryList *list, unsigned long encoded_index, const void *data);
void memory_fetch(MemoryList *list, unsigned long encoded_index, void *result);
void memory_free(MemoryList *list, const char *name);
unsigned long memory_get_type(unsigned long encoded_index);
unsigned long memory_find_by_name(MemoryList *list, const char *name);
// Fonctions d'affichage
void print_variable(MemoryList *list, const char *name);
void print_string(MemoryList *list, const char *name);
void print_array(MemoryList *list, const char *name);

// Fonction de débogage
// void send_to_channel(const char *msg);

#endif // MEMORY_FORTH_H
