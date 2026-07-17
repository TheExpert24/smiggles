#include "kernel.h"

// Simple spinlock implementation using GCC atomic builtins.
// Provides IRQ-save helpers for use in interrupt contexts.

void spinlock_init(spinlock_t* l) {
    if (!l) return;
    l->locked = 0;
}

void spin_lock(spinlock_t* l) {
    if (!l) return;
    while (__sync_lock_test_and_set(&l->locked, 1u)) {
        // busy-wait
        asm volatile("pause");
    }
    // Acquire barrier implicit in builtin
}

void spin_unlock(spinlock_t* l) {
    if (!l) return;
    __sync_lock_release(&l->locked);
}

unsigned int spin_lock_irqsave(spinlock_t* l) {
    unsigned int flags = 0;
    // Save EFLAGS
    asm volatile ("pushf; pop %0" : "=r" (flags) : : "memory");
    // Disable interrupts
    asm volatile ("cli" : : : "memory");
    spin_lock(l);
    return flags;
}

void spin_unlock_irqrestore(spinlock_t* l, unsigned int flags) {
    if (!l) return;
    spin_unlock(l);
    // Restore EFLAGS (including IF)
    asm volatile ("push %0; popf" : : "r" (flags) : "memory", "cc");
}
