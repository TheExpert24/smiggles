#include "kernel.h"

#define SYS_YIELD      0u
#define SYS_GET_TICKS  1u
#define SYS_GET_PID    2u
#define SYS_WAIT_TICKS 3u
#define SYS_SPAWN_DEMO 4u
#define SYS_KILL_PID   5u
#define SYS_GET_CPL    6u

unsigned int syscall_dispatch(unsigned int number, unsigned int arg0) {
    switch (number) {
        case SYS_YIELD:
            process_yield();
            return 0;
        case SYS_GET_TICKS:
            return (unsigned int)ticks;
        case SYS_GET_PID:
            if (current_process < 0) return 0xFFFFFFFFu;
            return (unsigned int)current_process;
        case SYS_WAIT_TICKS: {
            unsigned int start = (unsigned int)ticks;
            while (((unsigned int)ticks - start) < arg0) {
                process_yield();
            }
            return (unsigned int)ticks;
        }
        case SYS_SPAWN_DEMO:
            return (unsigned int)process_spawn_demo_with_work(arg0);
        case SYS_KILL_PID:
            return (unsigned int)process_kill((int)arg0);
        case SYS_GET_CPL:
            return protection_get_cpl();
        default:
            return 0xFFFFFFFFu;
    }
}

unsigned int syscall_invoke(unsigned int number) {
    return syscall_invoke1(number, 0);
}

unsigned int syscall_invoke1(unsigned int number, unsigned int arg0) {
    unsigned int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(number), "b"(arg0)
        : "memory"
    );
    return ret;
}
