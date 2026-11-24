#ifndef SMIGGLES_PROCESS_H
#define SMIGGLES_PROCESS_H

#include <stdint.h>

#define MAX_PATH_LENGTH 128
#define MAX_NAME_LENGTH 32
#define MAX_PROCESSES 64
#define MAX_PROCESS_MMAP_REGIONS 16

typedef struct {
    unsigned int addr;
    unsigned int size;
    unsigned int order;
    int in_use;
} ProcessMMapRegion;

typedef struct {
    unsigned int addr;
    unsigned int size;
    int in_use;
} ProcessElfRegion;

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
    char cwd[MAX_PATH_LENGTH];
    unsigned int page_directory;
    unsigned int esp;
    unsigned int eip;
    unsigned int exit_code;
    unsigned int wait_reaped;
    unsigned int block_reason;
    unsigned int block_arg;
    unsigned int stack_guard_base;
    unsigned int stack_base;
    unsigned int stack_size;
    unsigned int user_stack_base;
    unsigned int user_stack_size;
    unsigned int run_ticks;
    unsigned int regs[8];
    unsigned int pending_signals;
    unsigned int last_irq_eip;
    unsigned int last_irq_cs;
    unsigned int last_irq_eflags;
    unsigned int last_irq_esp;
    unsigned int last_irq_ss;
    int parent_pid;
    unsigned int brk_base;
    unsigned int brk_current;
    unsigned int brk_limit;
    ProcessMMapRegion mmap_regions[MAX_PROCESS_MMAP_REGIONS];
    ProcessElfRegion elf_regions[MAX_PROCESS_MMAP_REGIONS];
    int fork_context_valid;
    unsigned int fork_edi;
    unsigned int fork_esi;
    unsigned int fork_ebp;
    unsigned int fork_ebx;
    unsigned int fork_edx;
    unsigned int fork_ecx;
    unsigned int fork_eax;
    unsigned int fork_eip;
    unsigned int fork_cs;
    unsigned int fork_eflags;
    unsigned int fork_user_esp;
    unsigned int fork_user_ss;
} PCB;

typedef struct {
    unsigned int edi;
    unsigned int esi;
    unsigned int ebp;
    unsigned int esp_dummy;
    unsigned int ebx;
    unsigned int edx;
    unsigned int ecx;
    unsigned int eax;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
    unsigned int user_esp;
    unsigned int user_ss;
} IRQContext;

extern PCB process_table[MAX_PROCESSES];
extern int current_process;

void init_process_table(void);
void linux_close_pipes_for_pid(int pid);
int process_create(unsigned int entry_point);
void context_switch(int from_pid, int to_pid);
void schedule(void);
void process_exit(void);
void process_exit_with_status(unsigned int exit_code);
void process_block_current(unsigned int block_reason, unsigned int block_arg);
void process_wake_waiting(unsigned int block_reason);
void process_wake_waiting_with_arg(unsigned int block_reason, unsigned int arg);
int process_waitpid(int pid, unsigned int* status, int options);
void process_yield(void);
int process_kill(int pid);
int process_spawn_demo(void);
int process_spawn_demo_with_work(unsigned int work_ticks);
int process_spawn_ring3_demo(void);
int process_spawn_ring3_fault_demo(void);
void process_fork_resume_entry(void);
void process_set_demo_autorespawn(int enabled);
int process_get_demo_autorespawn(void);
void process_maintenance_tick(void);
const char* process_state_name(ProcessState state);
int process_send_signal(int pid, int signal_number);
void process_deliver_pending_signals(IRQContext* ctx);
void process_record_irq_context(const IRQContext* ctx);
int process_set_cwd(const char* cwd);
int process_get_cwd(char* out, unsigned int out_size);

#define PROC_WAIT_KEYBOARD 1u
#define PROC_WAIT_CHILD    2u
#define PROC_WAIT_TIMER    3u
#define PROC_WAIT_PIPE_READ 4u
#define PROC_WAIT_PIPE_WRITE 5u

#endif
