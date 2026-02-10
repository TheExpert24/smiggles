#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

// --- Common Definitions ---
#define MAX_PATH_LENGTH 128
#define MAX_NAME_LENGTH 32
#define MAX_CHILDREN 16
#define MAX_NODES 64
#define MAX_FILE_CONTENT 256
#define MAX_FILE_NAME 32
#define MAX_FILES 8
#define MAX_FILE_SIZE 128
#define MAX_DIRS 4
#define MAX_DIR_NAME 32

// --- Interrupt Definitions ---
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

struct IDT_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct IDT_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

// --- Filesystem Types ---
typedef enum {
    NODE_FILE,
    NODE_DIRECTORY
} NodeType;

typedef struct FSNode {
    char name[MAX_NAME_LENGTH];
    NodeType type;
    int parent_idx;
    int children_idx[MAX_CHILDREN];
    int child_count;
    char content[MAX_FILE_CONTENT];
    int content_size;
    int used;
} FSNode;

typedef struct {
    char name[MAX_DIR_NAME];
    int used;
    int parent;
} RamDir;

typedef struct {
    char name[MAX_FILE_NAME];
    char data[MAX_FILE_SIZE];
    int size;
    int dir;
} RamFile;

// --- Global Variables ---
extern volatile int ticks;
extern char last_key;
extern int just_saved;
extern int skip_next_prompt;
extern FSNode node_table[MAX_NODES];
extern int node_count;
extern int current_dir_idx;
extern RamDir dir_table[MAX_DIRS];
extern int dir_count;
extern int current_dir;
extern RamFile file_table[MAX_FILES];
extern int file_count;
extern char history[10][64];
extern int history_count;
extern int line_start;
extern int cmd_len;
extern int cmd_cursor;
extern int history_position;
extern int tab_completion_active;
extern int tab_completion_position;
extern int tab_match_count;
extern char tab_matches[32][32];

// --- Function Declarations ---

// Memory management
void init_paging(void);
void* alloc_page(void);
void free_page(void* addr);

// Interrupts
void pic_remap(void);
void set_idt_entry(int n, unsigned int handler);
void timer_handler(void);
void keyboard_handler(void);
extern void load_idt(void*);
extern void irq0_timer_handler();
extern void irq1_keyboard_handler();

// Filesystem
void init_filesystem(void);
int fs_mkdir(const char* path);
int fs_touch(const char* path, const char* content);
int fs_rm(const char* path, int recursive);
int resolve_path(const char* path);
void get_full_path(int node_idx, char* path, int max_len);
int parse_path(const char* path, char components[32][MAX_NAME_LENGTH], int* comp_count);

// String utilities
int str_len(const char* s);
void str_copy(char* dst, const char* src, int max);
int str_equal(const char* a, const char* b);
int mini_strcmp(const char* a, const char* b);
void int_to_str(int value, char* buf);
void str_concat(char* dest, const char* src);

// Display
void scroll_screen(char* video);
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_smiggles_art(char* video, int* cursor);

// Editor
void nano_editor(const char* filename, char* video, int* cursor);

// Calculator
int is_math_expr(const char* s);
void handle_calc_command(const char* expr, char* video, int* cursor);

// Commands
void dispatch_command(const char* cmd, char* video, int* cursor);
void add_to_history(const char* cmd);
int find_file(const char* name);
void handle_clear_command(char* video, int* cursor);
void handle_tab_completion(char* cmd_buf, int* cmd_len, int* cmd_cursor, char* video, int* cursor, int line_start);

// Time utilities
unsigned char cmos_read(unsigned char reg);
unsigned char bcd_to_bin(unsigned char bcd);
void get_time_string(char* buf);

#endif // KERNEL_H