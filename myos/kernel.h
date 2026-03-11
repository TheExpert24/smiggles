#define COLOR_BLACK         0x00
#define COLOR_BLUE          0x01
#define COLOR_GREEN         0x02
#define COLOR_CYAN          0x03
#define COLOR_RED           0x04
#define COLOR_MAGENTA       0x05
#define COLOR_BROWN         0x06
#define COLOR_LIGHT_GRAY    0x07
#define COLOR_DARK_GRAY     0x08
#define COLOR_LIGHT_BLUE    0x09
#define COLOR_LIGHT_GREEN   0x0A
#define COLOR_LIGHT_CYAN    0x0B
#define COLOR_LIGHT_RED     0x0C
#define COLOR_LIGHT_MAGENTA 0x0D
#define COLOR_YELLOW        0x0E
#define COLOR_WHITE         0x0F
#define FS_DISK_SECTOR 10 // Start sector for filesystem data
// Persistent filesystem image size (must be >= sizeof(struct FSImage) in filesystem.c)
#define FS_SECTOR_COUNT 320 // 320*512 = 163840 bytes

// Static assert to ensure persistent image is large enough for FSImage
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

// --- Common Definitions ---
#define MAX_PATH_LENGTH 128
#define MAX_NAME_LENGTH 32
#define MAX_CHILDREN 32
#define MAX_NODES 32
// --- User Authentication ---
#define MAX_USERS 8
typedef struct {
     char username[MAX_NAME_LENGTH];
     char password[MAX_NAME_LENGTH];
     int is_admin;
} User;

extern User user_table[MAX_USERS];
extern int user_count;
extern int current_user_idx; // -1 means no user logged in
#define MAX_FILE_CONTENT 2048
#define MAX_FILE_NAME 32
#define MAX_FILES 8
#define MAX_FILE_SIZE 128
#define MAX_DIRS 4
#define MAX_DIR_NAME 32
#define MAX_CMD_BUFFER 2048

// --- Process Management ---
#define MAX_PROCESSES 8
typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_EXITED
} ProcessState;

typedef struct {
    int pid;
    ProcessState state;
    char name[16];
    unsigned int esp;
    unsigned int eip;
    unsigned int stack_base;
    unsigned int stack_size;
    unsigned int run_ticks;
    unsigned int regs[8];
} PCB;

extern PCB process_table[MAX_PROCESSES];
extern int current_process;
void init_process_table(void);

// Create a new process
int process_create(unsigned int entry_point);

// Switch context between two processes
void context_switch(int from_pid, int to_pid);

// Simple round-robin scheduler
void schedule(void);

// Terminate current process
void process_exit(void);

// Voluntarily yield CPU
void process_yield(void);

// Run currently scheduled process for one scheduler tick
void process_run_current_tick(void);

// Spawn a demo background process
int process_spawn_demo(void);

// Spawn demo process with custom work ticks (0 = unlimited)
int process_spawn_demo_with_work(unsigned int work_ticks);

// Kill process by pid
int process_kill(int pid);

// Enable/disable auto-respawn for demo process
void process_set_demo_autorespawn(int enabled);
int process_get_demo_autorespawn(void);

// Periodic maintenance tasks for process subsystem
void process_maintenance_tick(void);

// Human-readable process state
const char* process_state_name(ProcessState state);

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
    int owner_idx; // index of owning user
    unsigned short permissions; // permission bits: rwx for owner/group/others
    } __attribute__((packed)) FSNode;

typedef struct {
    char name[MAX_DIR_NAME];
    int used;
    int parent;
    } __attribute__((packed)) RamDir;

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

typedef struct {
    int col;
    int row;
    int wheel_delta;
} MouseState;

// --- Function Declarations ---

// Memory management
void init_paging(void);
void* alloc_page(void);
void free_page(void* addr);

// Protection (GDT/TSS/Ring setup)
void init_protection(void);
int protection_is_ready(void);
unsigned int protection_get_cpl(void);

// Interrupts
void pic_remap(void);
void set_idt_entry(int n, unsigned int handler);
void set_idt_entry_user(int n, unsigned int handler);
void timer_handler(void);
void keyboard_handler(void);
void mouse_handler(void);
int keyboard_pop_scancode(unsigned char* out_scancode);
void mouse_init(void);
void mouse_poll_hardware(void);
int mouse_poll_state(MouseState* state);
extern void load_idt(void*);
extern void irq0_timer_handler();
extern void irq1_keyboard_handler();
extern void irq12_mouse_handler();
extern void isr_syscall_handler();

// Syscalls
unsigned int syscall_dispatch(unsigned int number, unsigned int arg0);
unsigned int syscall_invoke(unsigned int number);
unsigned int syscall_invoke1(unsigned int number, unsigned int arg0);

// Filesystem
void init_filesystem(void);
int fs_mkdir(const char* path);
int fs_touch(const char* path, const char* content);
int fs_rm(const char* path, int recursive);
int resolve_path(const char* path);
void get_full_path(int node_idx, char* path, int max_len);
int parse_path(const char* path, char components[32][MAX_NAME_LENGTH], int* comp_count);

// Persistent storage
void fs_save(void);
void fs_load(void);
void fs_get_status(uint32_t* active_generation_out, int slot_validity[2]);

// String utilities
int str_len(const char* s);
void str_copy(char* dst, const char* src, int max);
int str_equal(const char* a, const char* b);
int mini_strcmp(const char* a, const char* b);
void int_to_str(int value, char* buf);
void str_concat(char* dest, const char* src);

// Panic handling
void kernel_panic(const char* reason, const char* detail);
void exception_handler(unsigned int vector, unsigned int error_code, unsigned int eip, unsigned int cs, unsigned int eflags);
extern void (*exception_stub_table[32])(void);

// Display
void scroll_screen(char* video);
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_smiggles_art(char* video, int* cursor);
void set_cursor_position(int cursor);
char scancode_to_char(unsigned char scancode, int shift);
void display_init(char* video);
void display_hide_mouse(char* video);
void display_refresh_mouse(char* video);
void display_set_mouse_position(int col, int row);
int display_scroll_view(int delta, char* video);
int display_is_scrollback_active(void);
void display_sync_live_screen(char* video);
void display_restore_live_screen(char* video);

// Editor
void nano_editor(const char* filename, char* video, int* cursor);

// Calculator
int is_math_expr(const char* s);
void handle_calc_command(const char* expr, char* video, int* cursor);

// Tiny BASIC
void basic_repl(char* video, int* cursor);
int basic_run_file(const char* filename, char* video, int* cursor);

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

// Disk I/O for persistent storage
int disk_read_sector(unsigned int lba, void* buffer);
int disk_write_sector(unsigned int lba, const void* buffer);

// ATA PIO disk driver
int ata_read_sector(unsigned int lba, void* buffer);
int ata_write_sector(unsigned int lba, const void* buffer);

#endif // KERNEL_H

// I/O port functions for ATA
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned short inw(unsigned short port) {
    unsigned short ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(unsigned short port, unsigned short val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}