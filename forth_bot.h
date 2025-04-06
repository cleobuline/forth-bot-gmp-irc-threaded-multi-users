#ifndef FORTH_GLOBALS_H
#define FORTH_GLOBALS_H

#define STACK_SIZE 1000
#define WORD_CODE_SIZE 512
#define CONTROL_STACK_SIZE 100
#define MAX_STRING_SIZE 256
#define MPZ_POOL_SIZE 3
#define BUFFER_SIZE 2048
 
#define SERVER "46.16.175.175" // default 
#define PORT 6667
#define BOT_NAME "mforth"
#define USER "mforth"
#define CHANNEL "##forth"

 
typedef enum {
    OP_PUSH,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_DUP,
    OP_DROP,
    OP_SWAP,
    OP_OVER,
    OP_ROT,
    OP_TO_R,
    OP_FROM_R,
    OP_R_FETCH,
    OP_SEE,
    OP_2DROP,
    OP_EQ,
    OP_LT,
    OP_GT,
    OP_AND,
    OP_OR,
    OP_NOT,
    OP_XOR,
    OP_CALL,
    OP_BRANCH,
    OP_BRANCH_FALSE,
    OP_END,
    OP_DOT,
    OP_DOT_S,
    OP_EMIT,
    OP_CR,
    OP_VARIABLE,
    OP_STORE,
    OP_FETCH,
    OP_ALLOT,
    OP_DO,
    OP_LOOP,
    OP_I,
    OP_J,
    OP_UNLOOP,
    OP_PLUS_LOOP,
    OP_SQRT,
    OP_DOT_QUOTE,
    OP_CASE,
    OP_OF,
    OP_ENDOF,
    OP_ENDCASE,
    OP_EXIT,
    OP_BEGIN,
    OP_WHILE,
    OP_REPEAT,
    OP_UNTIL,
    OP_AGAIN,
    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_LSHIFT,
    OP_RSHIFT,
    OP_FORGET,
    OP_WORDS,
    OP_LOAD,
    OP_PICK,
    OP_ROLL,
    OP_PLUSSTORE,
    OP_DEPTH,
    OP_TOP,
    OP_NIP,
    OP_CREATE,
    OP_STRING,
    OP_QUOTE,
    OP_PRINT,
    OP_NUM_TO_BIN,
    OP_PRIME_TEST,
    OP_CLEAR_STACK,
    OP_CLOCK,
    OP_IMAGE,
    OP_TEMP_IMAGE,
    OP_CLEAR_STRINGS,
    OP_DELAY,
    OP_RECURSE,
    OP_CONSTANT
} OpCode;
typedef struct {
    OpCode opcode;
    long int operand;
} Instruction;

// Définir CompiledWord avant DynamicDictionary
typedef struct {
    char *name;
    Instruction code[WORD_CODE_SIZE];
    long int code_length;
    char *strings[WORD_CODE_SIZE];
    long int string_count;
    int immediate;
} CompiledWord;

typedef struct {
    CompiledWord *words;   // Pointeur vers les mots alloués dynamiquement
    long int count;        // Nombre de mots actuels
    long int capacity;     // Capacité actuelle du tableau
} DynamicDictionary;
 
 
// Structure pour une pile Forth
typedef struct {
    mpz_t data[STACK_SIZE];
    long int top;
} Stack;

// Structure pour les structures de contrôle
typedef enum { CT_IF, CT_ELSE, CT_DO, CT_CASE, CT_OF, CT_ENDOF, CT_BEGIN, CT_WHILE, CT_REPEAT } ControlType;

typedef struct {
    ControlType type;
    long int addr;
} ControlEntry;

// Structure pour la mémoire Forth
typedef enum {
    MEMORY_VARIABLE,
    MEMORY_ARRAY,
    MEMORY_STRING
} MemoryType;

typedef struct {
    char *name;
    MemoryType type;
    mpz_t *values;
    char *string;
    long int size;
} Memory;

#define LOOP_STACK_SIZE 16  // Taille max de la pile de boucles, ajustable

typedef struct {
    mpz_t index;    // Index actuel de la boucle
    mpz_t limit;    // Limite de la boucle
} LoopEntry;

#endif
// File d’attente pour les commandes
#define QUEUE_SIZE 100
typedef struct {
    char cmd[512];
    char nick[MAX_STRING_SIZE];
} Command;

// Structure Env pour le multi-utilisateur
typedef struct Env {
    char nick[MAX_STRING_SIZE];
    Stack main_stack;
    Stack return_stack;
    LoopEntry loop_stack[LOOP_STACK_SIZE];
    int loop_stack_top;
    DynamicDictionary dictionary; // Remplace CompiledWord dictionary[DICT_SIZE]
    MemoryList memory_list;
    char output_buffer[BUFFER_SIZE];
    int buffer_pos;
    CompiledWord currentWord;
    int compiling;
    int compile_error;
    long int current_word_index;
    ControlEntry control_stack[CONTROL_STACK_SIZE];
    int control_stack_top;
    char *string_stack[STACK_SIZE];
    int string_stack_top;
    int error_flag;
    char emit_buffer[512];
    int emit_buffer_pos;
    struct Env *next;
} Env;

void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index);
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index);
void interpret(char *input, Stack *stack);
void compileToken(char *token, char **input_rest, Env *env);
void send_to_channel(const char *msg) ;
void push(Stack *stack, mpz_t value);
void pop(Stack *stack, mpz_t result) ;
void push_string(char *str);
char *pop_string();
void set_error(const char *msg);
void print_word_definition_irc(int index, Stack *stack);
int findCompiledWordIndex(char *name) ;
void resizeDynamicDictionary(DynamicDictionary *dict);
void initDynamicDictionary(DynamicDictionary *dict);
void initDictionary(Env *env);
void initEnv(Env *env, const char *nick) ;
Env *createEnv(const char *nick);
void freeEnv(const char *nick);
Env *findEnv(const char *nick);
char *generate_image(const char *prompt);
char *generate_image_tiny(const char *prompt) ; 
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp);
size_t write_binary_callback(void *contents, size_t size, size_t nmemb, void *userp);

void push(Stack *stack, mpz_t value);
void pop(Stack *stack, mpz_t result);
void push_string(char *str);
char *pop_string() ;
void set_error(const char *msg);
void enqueue(const char *cmd, const char *nick);
void send_to_channel(const char *msg) ;
void irc_connect(const char *server_ip, const char *bot_nick, const char *channel)  ;
 void interpret(char *input, Stack *stack);
 void *interpret_thread(void *arg);
 void enqueue(const char *cmd, const char *nick);
 Command *dequeue();
extern Env *head;
extern Env *currentenv;
extern mpz_t mpz_pool[MPZ_POOL_SIZE];
extern char *channel;
extern int irc_socket;
