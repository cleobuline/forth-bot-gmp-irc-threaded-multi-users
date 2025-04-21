#ifndef FORTH_GLOBALS_H
#define FORTH_GLOBALS_H

#include <pthread.h>
#include <signal.h>

#define STACK_SIZE 1000
#define WORD_CODE_SIZE 512
#define CONTROL_STACK_SIZE 500
#define MAX_STRING_SIZE 256
#define MPZ_POOL_SIZE 3
#define BUFFER_SIZE 8000
#define SERVER "46.16.175.175"
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
    OP_CONSTANT,
    OP_MICRO,
    OP_MILLI,
    OP_APPEND
} OpCode;

#define IRC_MSG_QUEUE_SIZE 500
typedef struct {
    char msg[8000];
    int used;
} IrcMessage;

typedef struct {
    OpCode opcode;
    long int operand;
} Instruction;

typedef struct {
    char *name;
    Instruction *code;
    long int code_length;
    long int code_capacity;
    char **strings;
    long int string_count;
    long int string_capacity;
    int immediate;
} CompiledWord;

typedef struct {
    CompiledWord *words;
    long int count;
    long int capacity;
} DynamicDictionary;

typedef struct {
    mpz_t data[STACK_SIZE];
    long int top;
} Stack;

typedef enum { CT_IF, CT_ELSE, CT_DO, CT_CASE, CT_OF, CT_ENDOF, CT_BEGIN, CT_WHILE, CT_REPEAT } ControlType;

typedef struct {
    ControlType type;
    long int addr;
} ControlEntry;

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

#define LOOP_STACK_SIZE 16

typedef struct {
    mpz_t index;
    mpz_t limit;
} LoopEntry;

#define QUEUE_SIZE 100
typedef struct {
    char cmd[512];
    char nick[MAX_STRING_SIZE];
} Command;

typedef struct Env {
    char nick[MAX_STRING_SIZE];
    Stack main_stack;
    Stack return_stack;
    LoopEntry loop_stack[LOOP_STACK_SIZE];
    int loop_stack_top;
    DynamicDictionary dictionary;
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
    Command queue[QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    pthread_mutex_t queue_mutex;
    pthread_mutex_t in_use_mutex;
    pthread_t thread;
    pthread_cond_t queue_cond;
    int thread_running;
    int being_freed;
    mpz_t mpz_pool[MPZ_POOL_SIZE];
} Env;

struct irc_message {
    char *prefix;
    char *command;
    char **args;
    int arg_count;
};

extern volatile sig_atomic_t shutdown_flag;

#define SAFE_MALLOC(size) ({ \
    void *ptr = malloc(size); \
    if (!ptr) { \
        fprintf(stderr, "Fatal: Memory allocation failed\n"); \
        pthread_kill(main_thread, SIGUSR1); \
    } \
    ptr; \
})

#define SAFE_REALLOC(ptr, size) ({ \
    void *new_ptr = realloc(ptr, size); \
    if (!new_ptr) { \
        fprintf(stderr, "Fatal: Memory reallocation failed\n"); \
        pthread_kill(main_thread, SIGUSR1); \
    } \
    new_ptr; \
})

void parse_irc_message(const char *line, struct irc_message *msg);
void free_irc_message(struct irc_message *msg);
int irc_handle_message(const char *line, char *bot_nick, int *registered, char *nick_out, char *cmd_out, size_t cmd_out_size);
int irc_receive(char *buffer, size_t buffer_size, size_t *buffer_pos);
void executeInstruction(Instruction instr, Stack *stack, long int *ip, CompiledWord *word, int word_index, Env *env);
void executeCompiledWord(CompiledWord *word, Stack *stack, int word_index, Env *env);
void compileToken(char *token, char **input_rest, Env *env);
void interpret(char *input, Stack *stack, Env *env);
void freeCurrentWord(Env *env);
void print_word_definition_irc(int index, Stack *stack, Env *env);
int findCompiledWordIndex(char *name, Env *env);
void resizeDynamicDictionary(DynamicDictionary *dict);
void initDynamicDictionary(DynamicDictionary *dict);
void resizeCompiledWordArrays(CompiledWord *word, int is_code);
void initDictionary(Env *env);
Env *createEnv(const char *nick);
void freeEnv(const char *nick);
Env *findEnv(const char *nick);
char *generate_image(const char *prompt);
char *generate_image_tiny(const char *prompt);
size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp);
size_t write_binary_callback(void *contents, size_t size, size_t nmemb, void *userp);
void push(Env *env, mpz_t value);
void pop(Env *env, mpz_t result);
void push_string(Env *env, char *str);
char *pop_string(Env *env);
void set_error(Env *env, const char *msg);
void enqueue(const char *cmd, const char *nick);
void send_to_channel(const char *msg);
void irc_connect(const char *server_ip, const char *bot_nick);
void *env_interpret_thread(void *arg);
Command *dequeue(Env *env);
void *irc_sender_thread(void *arg);
void enqueue_irc_msg(const char *msg);

extern Env *head;
extern char *channel;
extern int irc_socket;
extern pthread_rwlock_t env_rwlock;
extern pthread_mutex_t irc_mutex;
extern pthread_t main_thread;
extern IrcMessage irc_msg_queue[IRC_MSG_QUEUE_SIZE];
extern int irc_msg_queue_head;
extern int irc_msg_queue_tail;
extern pthread_mutex_t irc_msg_queue_mutex;
extern pthread_cond_t irc_msg_queue_cond;

#endif
