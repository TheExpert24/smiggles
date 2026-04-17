// process.c: Basic process management foundation
#include "kernel.h"

// Global process table and current process
PCB process_table[MAX_PROCESSES];
int current_process = -1;
static int demo_autorespawn = 0;
static unsigned int kernel_idle_esp = 0;

#define PAGE_SIZE_BYTES 4096u
#define PAGE_FLAG_PRESENT 0x001u
#define PAGE_ADDR_MASK 0xFFFFF000u
#define SIGNAL_MASK(sig) (1u << ((unsigned int)((sig) & 31)))

static void process_demo_entry(void);
static void process_entry_trampoline(void);
static void ring3_demo_user_main(void);
static void ring3_fault_user_main(void);
static void process_release_resources(PCB* proc);
static void process_release_user_mappings(PCB* proc);

static int process_alloc_kernel_stack_with_guard(unsigned int* out_guard_base,
                                                 unsigned int* out_stack_base);
static int process_unmap_guard_page(unsigned int page_directory, unsigned int guard_base);

static int process_install_signal_trampoline(PCB* proc, IRQContext* ctx, int signal_number) {
    if (!proc || !ctx) return -1;
    if ((ctx->cs & 3u) != 3u) return -1;

    unsigned int tramp_size = 16u;
    if (ctx->user_esp < tramp_size + 16u) return -1;

    unsigned int tramp_addr = (ctx->user_esp - tramp_size) & ~0x3u;
    unsigned char* code = (unsigned char*)(uintptr_t)tramp_addr;
    unsigned int exit_code = 128u + (unsigned int)signal_number;

    // mov eax, LINUX_SYS_EXIT ; mov ebx, exit_code ; int 0x80 ; jmp .
    code[0] = 0xB8;
    code[1] = (unsigned char)(LINUX_SYS_EXIT & 0xFF);
    code[2] = (unsigned char)((LINUX_SYS_EXIT >> 8) & 0xFF);
    code[3] = (unsigned char)((LINUX_SYS_EXIT >> 16) & 0xFF);
    code[4] = (unsigned char)((LINUX_SYS_EXIT >> 24) & 0xFF);
    code[5] = 0xBB;
    code[6] = (unsigned char)(exit_code & 0xFF);
    code[7] = (unsigned char)((exit_code >> 8) & 0xFF);
    code[8] = (unsigned char)((exit_code >> 16) & 0xFF);
    code[9] = (unsigned char)((exit_code >> 24) & 0xFF);
    code[10] = 0xCD;
    code[11] = 0x80;
    code[12] = 0xEB;
    code[13] = 0xFE;
    code[14] = 0x90;
    code[15] = 0x90;

    ctx->eip = tramp_addr;
    ctx->user_esp = tramp_addr;
    proc->last_irq_eip = ctx->eip;
    proc->last_irq_esp = ctx->user_esp;
    return 0;
}

static void process_reap_exited(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (i == current_process) continue;
        if (process_table[i].state != PROC_EXITED) continue;
        if (process_table[i].stack_base == 0 &&
            process_table[i].user_stack_base == 0 &&
            process_table[i].page_directory == 0) {
            continue;
        }
        process_release_resources(&process_table[i]);
    }
}

static void process_release_resources(PCB* proc) {
    if (!proc) return;
    vfs_close_for_pid(proc->pid);
    fs_fd_close_for_pid(proc->pid);
    process_release_user_mappings(proc);
    if (proc->page_directory) {
        paging_destroy_process_directory(proc->page_directory);
        proc->page_directory = 0;
    }
    if (proc->stack_guard_base) {
        free_pages((void*)proc->stack_guard_base, 1u);
        proc->stack_guard_base = 0;
        proc->stack_base = 0;
    } else if (proc->stack_base) {
        free_page((void*)proc->stack_base);
        proc->stack_base = 0;
    }
    proc->stack_size = 0;
    if (proc->user_stack_base) {
        unsigned int stack_phys = 0;
        unsigned int stack_flags = 0;
        if (proc->page_directory &&
            paging_get_mapping(proc->page_directory,
                               proc->user_stack_base,
                               &stack_phys,
                               &stack_flags) == 0) {
            (void)stack_flags;
            free_page((void*)(stack_phys & 0xFFFFF000u));
        } else {
            free_page((void*)proc->user_stack_base);
        }
        proc->user_stack_base = 0;
    }
    proc->user_stack_size = 0;
}

static void process_release_user_mappings(PCB* proc) {
    if (!proc) return;

    if (proc->brk_base && proc->brk_limit > proc->brk_base) {
        unsigned int bytes = proc->brk_limit - proc->brk_base;
        unsigned int pages = bytes / PAGE_SIZE_BYTES;
        for (unsigned int p = 0; p < pages; p++) {
            free_page((void*)(proc->brk_base + p * PAGE_SIZE_BYTES));
        }
    }
    proc->brk_base = 0;
    proc->brk_current = 0;
    proc->brk_limit = 0;

    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!proc->mmap_regions[i].in_use) continue;
        unsigned int pages = proc->mmap_regions[i].size / PAGE_SIZE_BYTES;
        for (unsigned int p = 0; p < pages; p++) {
            free_page((void*)(proc->mmap_regions[i].addr + p * PAGE_SIZE_BYTES));
        }
        proc->mmap_regions[i].in_use = 0;
        proc->mmap_regions[i].addr = 0;
        proc->mmap_regions[i].size = 0;
        proc->mmap_regions[i].order = 0;
    }
}

static int process_alloc_kernel_stack_with_guard(unsigned int* out_guard_base,
                                                 unsigned int* out_stack_base) {
    if (!out_guard_base || !out_stack_base) return -1;

    unsigned int block = (unsigned int)alloc_pages(1u); // 2 contiguous pages
    if (!block) return -1;
    *out_guard_base = block;
    *out_stack_base = block + PAGE_SIZE_BYTES;
    return 0;
}

static int process_unmap_guard_page(unsigned int page_directory, unsigned int guard_base) {
    if (!page_directory || !guard_base) return -1;

    uint32_t* pd = (uint32_t*)(uintptr_t)page_directory;
    uint32_t pde_index = guard_base >> 22;
    uint32_t pte_index = (guard_base >> 12) & 0x3FFu;

    uint32_t pde = pd[pde_index];
    if ((pde & PAGE_FLAG_PRESENT) == 0) return -1;

    uint32_t* pt = (uint32_t*)(uintptr_t)(pde & PAGE_ADDR_MASK);
    pt[pte_index] &= ~PAGE_FLAG_PRESENT;
    return 0;
}

static int is_demo_process_active(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        ProcessState state = process_table[i].state;
        if (state == PROC_READY || state == PROC_RUNNING || state == PROC_BLOCKED) {
            if (str_equal(process_table[i].name, "demo")) {
                return 1;
            }
        }
    }
    return 0;
}

const char* process_state_name(ProcessState state) {
    switch (state) {
        case PROC_UNUSED: return "UNUSED";
        case PROC_READY: return "READY";
        case PROC_RUNNING: return "RUNNING";
        case PROC_BLOCKED: return "BLOCKED";
        case PROC_EXITED: return "EXITED";
        default: return "UNKNOWN";
    }
}

// Initialize process table
void init_process_table() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = i;
        process_table[i].state = PROC_UNUSED;
        process_table[i].name[0] = 0;
        process_table[i].page_directory = 0;
        process_table[i].esp = 0;
        process_table[i].eip = 0;
        process_table[i].stack_guard_base = 0;
        process_table[i].stack_base = 0;
        process_table[i].stack_size = 0;
        process_table[i].user_stack_base = 0;
        process_table[i].user_stack_size = 0;
        process_table[i].run_ticks = 0;
        for (int r = 0; r < 8; r++) process_table[i].regs[r] = 0;
        process_table[i].pending_signals = 0;
        process_table[i].last_irq_eip = 0;
        process_table[i].last_irq_cs = 0;
        process_table[i].last_irq_eflags = 0;
        process_table[i].last_irq_esp = 0;
        process_table[i].last_irq_ss = 0;
        process_table[i].parent_pid = -1;
        process_table[i].brk_base = 0;
        process_table[i].brk_current = 0;
        process_table[i].brk_limit = 0;
        process_table[i].fork_context_valid = 0;
        process_table[i].fork_edi = 0;
        process_table[i].fork_esi = 0;
        process_table[i].fork_ebp = 0;
        process_table[i].fork_ebx = 0;
        process_table[i].fork_edx = 0;
        process_table[i].fork_ecx = 0;
        process_table[i].fork_eax = 0;
        process_table[i].fork_eip = 0;
        process_table[i].fork_cs = 0;
        process_table[i].fork_eflags = 0;
        process_table[i].fork_user_esp = 0;
        process_table[i].fork_user_ss = 0;
        for (int m = 0; m < MAX_PROCESS_MMAP_REGIONS; m++) {
            process_table[i].mmap_regions[m].addr = 0;
            process_table[i].mmap_regions[m].size = 0;
            process_table[i].mmap_regions[m].order = 0;
            process_table[i].mmap_regions[m].in_use = 0;
        }
    }
    current_process = -1;
}

// Create a new process.  Allocates a 4 KiB kernel stack and pre-builds the
// saved-register frame that context_switch_asm will pop on the first switch:
//   [esp+0]  edi=0  [+4] esi=0  [+8] ebx=0  [+12] ebp=0
//   [+16] eflags=0x200 (IF set)  [+20] ret-addr = process_entry_trampoline
int process_create(unsigned int entry_point) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED || process_table[i].state == PROC_EXITED) {
            process_table[i].pid = i;
            process_table[i].state = PROC_READY;
            process_table[i].name[0] = 0;
            process_table[i].eip = entry_point;
            process_table[i].stack_size = PAGE_SIZE_BYTES;
            process_table[i].stack_guard_base = 0;
            process_table[i].user_stack_base = 0;
            process_table[i].user_stack_size = 0;

            unsigned int guard_base = 0;
            unsigned int stack_base = 0;
            if (process_alloc_kernel_stack_with_guard(&guard_base, &stack_base) != 0) {
                process_table[i].state = PROC_UNUSED;
                return -1;
            }
            process_table[i].stack_guard_base = guard_base;
            process_table[i].stack_base = stack_base;

            process_table[i].page_directory = paging_create_process_directory(0, 0, 0);
            if (!process_table[i].page_directory) {
                free_pages((void*)process_table[i].stack_guard_base, 1u);
                process_table[i].stack_base = 0;
                process_table[i].stack_guard_base = 0;
                process_table[i].stack_size = 0;
                process_table[i].state = PROC_UNUSED;
                return -1;
            }

            if (process_unmap_guard_page(process_table[i].page_directory,
                                         process_table[i].stack_guard_base) != 0) {
                paging_destroy_process_directory(process_table[i].page_directory);
                free_pages((void*)process_table[i].stack_guard_base, 1u);
                process_table[i].page_directory = 0;
                process_table[i].stack_base = 0;
                process_table[i].stack_guard_base = 0;
                process_table[i].stack_size = 0;
                process_table[i].state = PROC_UNUSED;
                return -1;
            }

            unsigned int* sp = (unsigned int*)(process_table[i].stack_base
                                               + process_table[i].stack_size);
            *--sp = (unsigned int)process_entry_trampoline;
            *--sp = 0x200;          // eflags: IF=1
            *--sp = 0;              // ebp
            *--sp = 0;              // ebx
            *--sp = 0;              // esi
            *--sp = 0;              // edi  ← esp starts here
            process_table[i].esp = (unsigned int)sp;
            process_table[i].run_ticks = 0;
            for (int r = 0; r < 8; r++) process_table[i].regs[r] = 0;
            process_table[i].pending_signals = 0;
            process_table[i].last_irq_eip = 0;
            process_table[i].last_irq_cs = 0;
            process_table[i].last_irq_eflags = 0;
            process_table[i].last_irq_esp = 0;
            process_table[i].last_irq_ss = 0;
            process_table[i].parent_pid = -1;
            process_table[i].brk_base = 0;
            process_table[i].brk_current = 0;
            process_table[i].brk_limit = 0;
            process_table[i].fork_context_valid = 0;
            process_table[i].fork_edi = 0;
            process_table[i].fork_esi = 0;
            process_table[i].fork_ebp = 0;
            process_table[i].fork_ebx = 0;
            process_table[i].fork_edx = 0;
            process_table[i].fork_ecx = 0;
            process_table[i].fork_eax = 0;
            process_table[i].fork_eip = 0;
            process_table[i].fork_cs = 0;
            process_table[i].fork_eflags = 0;
            process_table[i].fork_user_esp = 0;
            process_table[i].fork_user_ss = 0;
            for (int m = 0; m < MAX_PROCESS_MMAP_REGIONS; m++) {
                process_table[i].mmap_regions[m].addr = 0;
                process_table[i].mmap_regions[m].size = 0;
                process_table[i].mmap_regions[m].order = 0;
                process_table[i].mmap_regions[m].in_use = 0;
            }
            return i;
        }
    }
    return -1;
}

void process_fork_resume_entry(void) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) {
        while (1) { }
    }

    PCB* proc = &process_table[current_process];
    if (!proc->fork_context_valid) {
        process_exit();
        while (1) { }
    }

    IRQContext ctx;
    ctx.edi = proc->fork_edi;
    ctx.esi = proc->fork_esi;
    ctx.ebp = proc->fork_ebp;
    ctx.esp_dummy = 0;
    ctx.ebx = proc->fork_ebx;
    ctx.edx = proc->fork_edx;
    ctx.ecx = proc->fork_ecx;
    ctx.eax = 0u;
    ctx.eip = proc->fork_eip;
    ctx.cs = proc->fork_cs;
    ctx.eflags = proc->fork_eflags;
    ctx.user_esp = proc->fork_user_esp;
    ctx.user_ss = proc->fork_user_ss;

    proc->fork_context_valid = 0;
    resume_from_irq_context(&ctx);
    while (1) { }
}

static unsigned int user_linux_int80_1(unsigned int nr, unsigned int a0) {
    unsigned int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a0) : "memory");
    return ret;
}

static void ring3_demo_user_main(void) {
    user_linux_int80_1(LINUX_SYS_EXIT, 0u);
    while (1) { }
}

static void ring3_fault_user_main(void) {
    volatile unsigned int* kernel_ptr = (volatile unsigned int*)0x00100000u;
    *kernel_ptr = 0xC0DEC0DEu;
    user_linux_int80_1(LINUX_SYS_EXIT, 0u);
    while (1) { }
}

static void process_entry_trampoline(void) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) {
        while (1) { }
    }

    PCB* proc = &process_table[current_process];
    void (*entry)(void) = (void (*)(void))proc->eip;

    if (proc->regs[7]) {
        unsigned int user_esp = proc->user_stack_base + proc->user_stack_size - 16;
        jump_to_ring3(proc->eip, user_esp);
        while (1) { }
    }

    if (entry) {
        entry();
    }

    process_exit();
    while (1) { }
}


// Real cooperative context switch.  Saves callee-saved regs + eflags onto
// the current stack via context_switch_asm, stores the resulting ESP in
// from_pid's PCB (or kernel_idle_esp when called with no current process),
// then loads to_pid's saved stack pointer and restores its context.
// Returns *inside to_pid's context*; returns to the caller when context
// switches back to from_pid later.
void context_switch(int from_pid, int to_pid) {
    if (to_pid < 0 || to_pid >= MAX_PROCESSES) return;
    PCB* to   = &process_table[to_pid];
    PCB* from = (from_pid >= 0 && from_pid < MAX_PROCESSES)
                    ? &process_table[from_pid] : 0;

    if (from && from->state == PROC_RUNNING)
        from->state = PROC_READY;
    to->state       = PROC_RUNNING;
    current_process = to_pid;
    protection_set_kernel_stack(to->stack_base + to->stack_size);

    unsigned int* save_ptr = from ? &from->esp : &kernel_idle_esp;
    unsigned int to_cr3 = to->page_directory ? to->page_directory : paging_get_kernel_directory();
    context_switch_asm(save_ptr, to->esp, to_cr3);
    // Execution continues here when from_pid is scheduled again
}


// Simple round-robin scheduler
void schedule(void) {
    int next = -1;
    int start = current_process;

    if (start < 0 || start >= MAX_PROCESSES) start = 0;
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (start + i) % MAX_PROCESSES;
        if (process_table[idx].state == PROC_READY) {
            next = idx;
            break;
        }
    }
    if (next != -1) {
        if (next != current_process || current_process == -1) {
            context_switch(current_process, next);
        } else {
            process_table[current_process].state = PROC_RUNNING;
        }
        return;
    }

    if (current_process >= 0) {
        PCB* from = &process_table[current_process];
        int old_pid = current_process;
        current_process = -1;
        protection_set_kernel_stack(0x90000);
        context_switch_asm(&from->esp, kernel_idle_esp, paging_get_kernel_directory());
        // resumes here when this process is scheduled again
        current_process = old_pid;
    }
}


// Terminate current process
void process_exit(void) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) return;
    PCB* proc = &process_table[current_process];
    proc->state = PROC_EXITED;
    proc->run_ticks++;
    schedule();
    while (1) { }
}

// Voluntarily yield CPU
void process_yield(void) {
    schedule();
}

int process_kill(int pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return -1;
    PCB* proc = &process_table[pid];
    if (proc->state == PROC_UNUSED) return -2;
    if (proc->state == PROC_EXITED) return 0;
    proc->pending_signals |= SIGNAL_MASK(LINUX_SIGTERM);
    if (pid != current_process) {
        proc->state = PROC_EXITED;
        process_release_resources(proc);
    }
    if (pid == current_process) {
        current_process = -1;
        schedule();
    }
    return 0;
}

// process_run_current_tick() has been removed.
// Processes now run on their own stacks via context_switch_asm.

static void process_demo_entry(void) {
    while (current_process >= 0 && current_process < MAX_PROCESSES) {
        PCB* proc = &process_table[current_process];
        proc->regs[0]++;
        proc->run_ticks++;
        if (proc->regs[1] > 0 && proc->regs[0] >= proc->regs[1]) {
            break;
        }
        process_yield();
    }
}

int process_spawn_demo(void) {
    return process_spawn_demo_with_work(200);
}

int process_spawn_demo_with_work(unsigned int work_ticks) {
    int pid = process_create((unsigned int)process_demo_entry);
    if (pid < 0) return pid;

    PCB* proc = &process_table[pid];
    str_copy(proc->name, "demo", (int)sizeof(proc->name));
    proc->regs[0] = 0;
    proc->regs[1] = work_ticks;
    proc->regs[7] = 0;
    return pid;
}

int process_spawn_ring3_demo(void) {
    int pid = process_create((unsigned int)ring3_demo_user_main);
    if (pid < 0) return pid;
    PCB* proc = &process_table[pid];

    void* user_stack = alloc_page();
    if (!user_stack) {
        process_kill(pid);
        return -1;
    }

    unsigned int user_pd = paging_create_process_directory(proc->eip,
                                                           (unsigned int)user_stack,
                                                           4096);
    if (!user_pd) {
        free_page(user_stack);
        process_kill(pid);
        return -1;
    }

    paging_destroy_process_directory(proc->page_directory);
    proc->page_directory = user_pd;
    process_unmap_guard_page(proc->page_directory, proc->stack_guard_base);
    proc->user_stack_base = (unsigned int)user_stack;
    proc->user_stack_size = 4096;

    str_copy(proc->name, "ring3", (int)sizeof(proc->name));
    proc->regs[7] = 1;
    return pid;
}

int process_spawn_ring3_fault_demo(void) {
    int pid = process_create((unsigned int)ring3_fault_user_main);
    if (pid < 0) return pid;
    PCB* proc = &process_table[pid];

    void* user_stack = alloc_page();
    if (!user_stack) {
        process_kill(pid);
        return -1;
    }

    unsigned int user_pd = paging_create_process_directory(proc->eip,
                                                           (unsigned int)user_stack,
                                                           4096);
    if (!user_pd) {
        free_page(user_stack);
        process_kill(pid);
        return -1;
    }

    paging_destroy_process_directory(proc->page_directory);
    proc->page_directory = user_pd;
    process_unmap_guard_page(proc->page_directory, proc->stack_guard_base);
    proc->user_stack_base = (unsigned int)user_stack;
    proc->user_stack_size = 4096;

    str_copy(proc->name, "ring3pf", (int)sizeof(proc->name));
    proc->regs[7] = 1;
    return pid;
}

void process_set_demo_autorespawn(int enabled) {
    demo_autorespawn = enabled ? 1 : 0;
}

int process_get_demo_autorespawn(void) {
    return demo_autorespawn;
}

// Called from the timer IRQ.  Only spawns; does NOT call schedule() because
// context_switch_asm cannot safely run inside the IRQ pusha/popa wrapper.
void process_maintenance_tick(void) {
    process_reap_exited();
    if (!demo_autorespawn) return;
    if (is_demo_process_active()) return;
    process_spawn_demo();
}

int process_send_signal(int pid, int signal_number) {
    if (pid < 0 || pid >= MAX_PROCESSES) return -1;
    if (signal_number <= 0 || signal_number >= 32) return -1;
    PCB* proc = &process_table[pid];
    if (proc->state == PROC_UNUSED || proc->state == PROC_EXITED) return -1;
    proc->pending_signals |= SIGNAL_MASK(signal_number);
    return 0;
}

void process_record_irq_context(const IRQContext* ctx) {
    if (!ctx) return;
    if (current_process < 0 || current_process >= MAX_PROCESSES) return;

    PCB* proc = &process_table[current_process];
    proc->last_irq_eip = ctx->eip;
    proc->last_irq_cs = ctx->cs;
    proc->last_irq_eflags = ctx->eflags;
    proc->last_irq_esp = ((ctx->cs & 3u) == 3u) ? ctx->user_esp : proc->esp;
    proc->last_irq_ss = ((ctx->cs & 3u) == 3u) ? ctx->user_ss : 0x10u;
}

void process_deliver_pending_signals(IRQContext* ctx) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) return;
    PCB* proc = &process_table[current_process];
    unsigned int pending = proc->pending_signals;
    if (!pending) return;

    // Minimal default actions until user handlers/trampoline are added.
    if (pending & SIGNAL_MASK(LINUX_SIGCHLD)) {
        proc->pending_signals &= ~SIGNAL_MASK(LINUX_SIGCHLD);
    }
    if (pending & SIGNAL_MASK(LINUX_SIGINT)) {
        proc->pending_signals &= ~SIGNAL_MASK(LINUX_SIGINT);
        if (!ctx || process_install_signal_trampoline(proc, ctx, LINUX_SIGINT) != 0) {
            process_exit();
        }
        return;
    }

    if (pending & SIGNAL_MASK(LINUX_SIGTERM)) {
        proc->pending_signals &= ~SIGNAL_MASK(LINUX_SIGTERM);
        if (!ctx || process_install_signal_trampoline(proc, ctx, LINUX_SIGTERM) != 0) {
            process_exit();
        }
        return;
    }

    if (pending & SIGNAL_MASK(LINUX_SIGPIPE)) {
        proc->pending_signals &= ~SIGNAL_MASK(LINUX_SIGPIPE);
        if (!ctx || process_install_signal_trampoline(proc, ctx, LINUX_SIGPIPE) != 0) {
            process_exit();
        }
        return;
    }
}
