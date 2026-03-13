#include "kernel.h"

unsigned int syscall_dispatch(unsigned int number, unsigned int arg0, unsigned int arg1, unsigned int arg2) {
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
        case SYS_OPEN:
            return (unsigned int)fs_fd_open((const char*)arg0, (int)arg1);
        case SYS_CLOSE:
            return (unsigned int)fs_fd_close((int)arg0);
        case SYS_READ:
            return (unsigned int)fs_fd_read((int)arg0, (char*)arg1, (int)arg2);
        case SYS_WRITE:
            return (unsigned int)fs_fd_write((int)arg0, (const char*)arg1, (int)arg2);
        default:
            return 0xFFFFFFFFu;
    }
}

unsigned int syscall_invoke(unsigned int number) {
    return syscall_invoke1(number, 0);
}

unsigned int syscall_invoke1(unsigned int number, unsigned int arg0) {
    return syscall_invoke2(number, arg0, 0);
}

unsigned int syscall_invoke2(unsigned int number, unsigned int arg0, unsigned int arg1) {
    return syscall_invoke3(number, arg0, arg1, 0);
}

unsigned int syscall_invoke3(unsigned int number, unsigned int arg0, unsigned int arg1, unsigned int arg2) {
    unsigned int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2)
        : "memory"
    );
    return ret;
}
