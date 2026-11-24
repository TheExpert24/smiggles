#ifndef SMIGGLES_SYSCALL_H
#define SMIGGLES_SYSCALL_H

#include "process.h"

#define SYS_YIELD      0u
#define SYS_GET_TICKS  1u
#define SYS_GET_PID    2u
#define SYS_WAIT_TICKS 3u
#define SYS_SPAWN_DEMO 4u
#define SYS_KILL_PID   5u
#define SYS_GET_CPL    6u
#define SYS_OPEN       7u
#define SYS_CLOSE      8u
#define SYS_READ       9u
#define SYS_WRITE      10u

#define LINUX_SYS_EXIT          1
#define LINUX_SYS_PIPE          42
#define LINUX_SYS_UNLINK        10
#define LINUX_SYS_RENAME        38
#define LINUX_SYS_FORK          2
#define LINUX_SYS_READ          3
#define LINUX_SYS_WRITE         4
#define LINUX_SYS_OPEN          5
#define LINUX_SYS_CLOSE         6
#define LINUX_SYS_WAITPID       7
#define LINUX_SYS_EXECVE        11
#define LINUX_SYS_CHDIR         12
#define LINUX_SYS_LSEEK         19
#define LINUX_SYS_GETPID        20
#define LINUX_SYS_GETUID        24
#define LINUX_SYS_GETGID        47
#define LINUX_SYS_GETEUID       49
#define LINUX_SYS_GETEGID       50
#define LINUX_SYS_BRK           45
#define LINUX_SYS_STAT          106
#define LINUX_SYS_FSTAT         108
#define LINUX_SYS_MKDIR         39
#define LINUX_SYS_RMDIR         40
#define LINUX_SYS_GETCWD        183
#define LINUX_SYS_IOCTL         54
#define LINUX_SYS_FSYNC         118
#define LINUX_SYS_MMAP          90
#define LINUX_SYS_MUNMAP        91
#define LINUX_SYS_POLL          168
#define LINUX_SYS_UNAME         122
#define LINUX_SYS_WRITEV        146
#define LINUX_SYS_EXIT_GROUP    252

#define LINUX_SIGINT  2
#define LINUX_SIGPIPE 13
#define LINUX_SIGTERM 15
#define LINUX_SIGCHLD 17

#define LINUX_EPERM  (-1)
#define LINUX_ENOENT (-2)
#define LINUX_EIO    (-5)
#define LINUX_EBADF  (-9)
#define LINUX_ECHILD (-10)
#define LINUX_EAGAIN (-11)
#define LINUX_ENOMEM (-12)
#define LINUX_EFAULT (-14)
#define LINUX_EEXIST (-17)
#define LINUX_EINVAL (-22)
#define LINUX_EMFILE (-24)
#define LINUX_ERANGE (-34)
#define LINUX_EPIPE  (-32)
#define LINUX_ENOSYS (-38)

#define LINUX_POLLIN  0x0001u
#define LINUX_POLLPRI 0x0002u
#define LINUX_POLLOUT 0x0004u
#define LINUX_POLLERR 0x0008u
#define LINUX_POLLHUP 0x0010u

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} LinuxUtsname;

#define PROC_RING_KERNEL 0
#define PROC_RING_USER   3

unsigned int syscall_dispatch(unsigned int number,
                              unsigned int arg0,
                              unsigned int arg1,
                              unsigned int arg2,
                              unsigned int saved_cs,
                              IRQContext* ctx);
unsigned int linux_syscall_dispatch(unsigned int number,
                                    unsigned int arg0,
                                    unsigned int arg1,
                                    unsigned int arg2,
                                    IRQContext* ctx);
unsigned int syscall_invoke(unsigned int number);
unsigned int syscall_invoke1(unsigned int number, unsigned int arg0);
unsigned int syscall_invoke2(unsigned int number, unsigned int arg0, unsigned int arg1);
unsigned int syscall_invoke3(unsigned int number, unsigned int arg0, unsigned int arg1, unsigned int arg2);

#endif
