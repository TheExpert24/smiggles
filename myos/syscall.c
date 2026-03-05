#include "kernel.h"

#define SYS_YIELD      0u
#define SYS_GET_TICKS  1u
#define SYS_GET_PID    2u

unsigned int syscall_dispatch(unsigned int number) {
    switch (number) {
        case SYS_YIELD:
            process_yield();
            return 0;
        case SYS_GET_TICKS:
            return (unsigned int)ticks;
        case SYS_GET_PID:
            if (current_process < 0) return 0xFFFFFFFFu;
            return (unsigned int)current_process;
        default:
            return 0xFFFFFFFFu;
    }
}

unsigned int syscall_invoke(unsigned int number) {
    unsigned int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(number)
        : "memory"
    );
    return ret;
}
