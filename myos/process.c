// process.c: Basic process management foundation
#include "kernel.h"

// Global process table and current process
PCB process_table[MAX_PROCESSES];
int current_process = -1;

// Initialize process table
void init_process_table() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = i;
        process_table[i].state = PROC_UNUSED;
        process_table[i].esp = 0;
        process_table[i].eip = 0;
        process_table[i].stack_base = 0;
        process_table[i].stack_size = 0;
        for (int r = 0; r < 8; r++) process_table[i].regs[r] = 0;
    }
    current_process = -1;
}

// Create a new process with given entry point
int process_create(unsigned int entry_point) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            process_table[i].pid = i;
            process_table[i].state = PROC_READY;
            process_table[i].eip = entry_point;
            process_table[i].stack_size = 4096; // 4KB stack
            // Allocate stack (identity-mapped)
            void* stack = alloc_page();
            if (!stack) return -1;
            process_table[i].stack_base = (unsigned int)stack;
            process_table[i].esp = process_table[i].stack_base + process_table[i].stack_size - 4;
            for (int r = 0; r < 8; r++) process_table[i].regs[r] = 0;
            return i;
        }
    }
    return -1; // No free slot
}


// Save and restore context between two processes
void context_switch(int from_pid, int to_pid) {
    // Save current process state (from_pid)
    // In a real OS, you would use inline assembly to save registers and stack pointer
    // Here, we just simulate by storing esp/eip (for demonstration)
    // TODO: Replace with real register save/restore
    if (to_pid < 0 || to_pid >= MAX_PROCESSES) return;
    PCB* to = &process_table[to_pid];
    PCB* from = 0;
    if (from_pid >= 0 && from_pid < MAX_PROCESSES) {
        from = &process_table[from_pid];
    }
    // Save current ESP/EIP (simulated)
    // from->esp = ...; from->eip = ...;
    // Restore next process ESP/EIP (simulated)
    // ... set CPU registers to to->esp, to->eip ...
    // Mark states
    if (from && from->state == PROC_RUNNING) {
        from->state = PROC_READY;
    }
    to->state = PROC_RUNNING;
    current_process = to_pid;
    // In a real OS, you would jump to to->eip and set esp
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
    if (next != -1 && next != current_process) {
        context_switch(current_process, next);
    } else if (next != -1 && current_process == -1) {
        process_table[next].state = PROC_RUNNING;
        current_process = next;
    }
}


// Terminate current process
void process_exit(void) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) return;
    PCB* proc = &process_table[current_process];
    proc->state = PROC_EXITED;
    // Free stack
    if (proc->stack_base) free_page((void*)proc->stack_base);
    proc->stack_base = 0;
    proc->stack_size = 0;
    schedule();
}

// Voluntarily yield CPU
void process_yield(void) {
    schedule();
}
