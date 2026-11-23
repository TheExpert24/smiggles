#include "kernel.h"

// gna be able to run sm 32-bit Linux elf binaries 
// TODO ELF loader


static void sc_memcpy(void* dst, const void* src, int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}
static void sc_memset(void* dst, unsigned char val, int n) {
    unsigned char* d = (unsigned char*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

// New block-based filesystem path (kept separate from legacy filesystem.c symbols)
extern void newfs_fd_init(void);
extern int newfs_stat(const char* path, FInode* stat_out);
extern int newfs_mkdir(const char* path);
extern int disk_fs_init(void);
extern int disk_fs_format(void);

static int vfs_ready = 0;
static int ensure_newfs_layout(void) {
    FInode tmp_inode;
    if (newfs_stat("/tmp", &tmp_inode) >= 0) {
        return 1;
    }
    if (newfs_mkdir("/tmp") >= 0) {
        return 1;
    }
    return 0;
}

int fs_runtime_ensure_newfs(void) {
    if (vfs_ready) return 1;

    if (disk_fs_init() != 0) {
        // Hybrid ISO boots on real USB media may have no persistent writable disk.
        // In that case, continue in degraded mode rather than hard-failing the boot.
        if (disk_fs_format() != 0) {
            if (vfs_init() != 0) return 0;
            vfs_ready = 1;
            return 1;
        }
        if (disk_fs_init() != 0) {
            if (vfs_init() != 0) return 0;
            vfs_ready = 1;
            return 1;
        }
    }

    if (!ensure_newfs_layout()) return 0;

    // Initialize underlying fd table used by disk backend and then VFS mounts.
    newfs_fd_init();

    if (vfs_init() != 0) return 0;

    vfs_ready = 1;
    return 1;
}

static int ensure_newfs_ready(void) {
    return fs_runtime_ensure_newfs();
}

typedef struct {
    unsigned int addr;
    unsigned int len;
    unsigned int prot;
    unsigned int flags;
    unsigned int fd;
    unsigned int offset;
} LinuxMmapArgs;

static unsigned int align_up_u32(unsigned int value, unsigned int align) {
    return (value + align - 1u) & ~(align - 1u);
}

static unsigned int pages_to_order(unsigned int pages) {
    unsigned int order = 0;
    unsigned int size = 1;
    while (size < pages) {
        size <<= 1;
        order++;
    }
    return order;
}

static PCB* current_proc(void) {
    if (current_process < 0 || current_process >= MAX_PROCESSES) return 0;
    return &process_table[current_process];
}

static int linux_resolve_path(const char* raw, char* out, unsigned int out_size) {
    if (!raw || !out || out_size < 2) return -1;
    PCB* proc = current_proc();
    char combined[MAX_PATH_LENGTH];

    if (raw[0] == '/') {
        if (str_len(raw) >= (int)sizeof(combined)) return -1;
        str_copy(combined, raw, (int)sizeof(combined));
    } else {
        const char* base = proc ? proc->cwd : "/";
        if (str_len(raw) + str_len(base) + 2 >= (int)sizeof(combined)) return -1;
        if (str_equal(base, "/")) {
            combined[0] = '/';
            combined[1] = 0;
            str_concat(combined, raw);
        } else {
            str_copy(combined, base, (int)sizeof(combined));
            str_concat(combined, "/");
            str_concat(combined, raw);
        }
    }

    if (!fs_path_normalize(combined, out, (int)out_size)) return -1;
    return 0;
}

#define MAX_PIPES 16
#define PIPE_DATA_SIZE 4096
#define LINUX_PIPE_FD_BASE 256

typedef struct {
    unsigned char data[PIPE_DATA_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned int count;
    unsigned int readers;
    unsigned int writers;
    unsigned int used;
} LinuxPipe;

typedef struct {
    unsigned int used;
    unsigned int pipe_index;
    unsigned int is_write;
    int owner_pid;
} LinuxPipeFd;

static LinuxPipe linux_pipes[MAX_PIPES];
static LinuxPipeFd linux_pipe_fds[256];

static int linux_pipe_fd_alloc(unsigned int pipe_index, unsigned int is_write) {
    for (unsigned int i = 0; i < 256u; i++) {
        if (linux_pipe_fds[i].used) continue;
        linux_pipe_fds[i].used = 1;
        linux_pipe_fds[i].pipe_index = pipe_index;
        linux_pipe_fds[i].is_write = is_write;
        linux_pipe_fds[i].owner_pid = (current_process >= 0) ?
            process_table[current_process].pid : -1;
        linux_pipes[pipe_index].used = 1;
        if (is_write) linux_pipes[pipe_index].writers++;
        else linux_pipes[pipe_index].readers++;
        return (int)(LINUX_PIPE_FD_BASE + i);
    }
    return -1;
}

static LinuxPipeFd* linux_pipe_fd_get(int fd) {
    if (fd < LINUX_PIPE_FD_BASE || fd >= LINUX_PIPE_FD_BASE + 256) return 0;
    LinuxPipeFd* entry = &linux_pipe_fds[fd - LINUX_PIPE_FD_BASE];
    return entry->used ? entry : 0;
}

void linux_close_pipes_for_pid(int pid) {
    for (unsigned int i = 0; i < 256u; i++) {
        LinuxPipeFd* pipe_fd = &linux_pipe_fds[i];
        if (!pipe_fd->used || pipe_fd->owner_pid != pid) continue;
        LinuxPipe* pipe = &linux_pipes[pipe_fd->pipe_index];
        if (pipe_fd->is_write && pipe->writers > 0u) pipe->writers--;
        if (!pipe_fd->is_write && pipe->readers > 0u) pipe->readers--;
        pipe_fd->used = 0;
        pipe_fd->owner_pid = -1;
        if (pipe->readers == 0u && pipe->writers == 0u) {
            pipe->used = 0;
            pipe->count = 0;
            pipe->head = 0;
            pipe->tail = 0;
        }
    }
    process_wake_waiting(PROC_WAIT_PIPE_READ);
    process_wake_waiting(PROC_WAIT_PIPE_WRITE);
}

static int find_free_mmap_slot(PCB* proc) {
    if (!proc) return -1;
    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!proc->mmap_regions[i].in_use) return i;
    }
    return -1;
}

static int share_cow_page(unsigned int parent_pd, unsigned int child_pd, unsigned int vaddr) {
    unsigned int phys = 0;
    unsigned int flags = 0;
    if (paging_get_mapping(parent_pd, vaddr, &phys, &flags) != 0) return -1;

    unsigned int cow_flags = (flags | PAGE_FLAG_USER | PAGE_FLAG_COW) & ~PAGE_FLAG_RW;
    if (paging_map_page(child_pd, vaddr, phys, cow_flags) != 0) return -1;
    if (paging_map_page(parent_pd, vaddr, phys, cow_flags) != 0) return -1;

    paging_inc_page_ref(phys & 0xFFFFF000u);
    return 0;
}

static void release_proc_user_allocations(PCB* proc) {
    if (!proc) return;

    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!proc->elf_regions[i].in_use) continue;
        unsigned int pages = proc->elf_regions[i].size / 4096u;
        for (unsigned int p = 0; p < pages; p++) {
            unsigned int vaddr = proc->elf_regions[i].addr + p * 4096u;
            unsigned int phys = 0;
            unsigned int flags = 0;
            if (proc->page_directory &&
                paging_get_mapping(proc->page_directory, vaddr, &phys, &flags) == 0) {
                free_page((void*)(phys & 0xFFFFF000u));
            }
        }
        proc->elf_regions[i].in_use = 0;
        proc->elf_regions[i].addr = 0;
        proc->elf_regions[i].size = 0;
    }

    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!proc->mmap_regions[i].in_use) continue;
        unsigned int pages = proc->mmap_regions[i].size / 4096u;
        for (unsigned int p = 0; p < pages; p++) {
            free_page((void*)(proc->mmap_regions[i].addr + p * 4096u));
        }
        proc->mmap_regions[i].in_use = 0;
        proc->mmap_regions[i].addr = 0;
        proc->mmap_regions[i].size = 0;
        proc->mmap_regions[i].order = 0;
    }

    if (proc->brk_base && proc->brk_limit > proc->brk_base) {
        unsigned int pages = (proc->brk_limit - proc->brk_base) / 4096u;
        for (unsigned int p = 0; p < pages; p++) {
            free_page((void*)(proc->brk_base + p * 4096u));
        }
    }
    proc->brk_base = 0;
    proc->brk_current = 0;
    proc->brk_limit = 0;
}

static int clone_user_mappings(PCB* parent, PCB* child) {
    if (!parent || !child) return -1;

    if (parent->user_stack_base && parent->user_stack_size) {
        unsigned int parent_phys = 0;
        unsigned int parent_flags = 0;
        if (paging_get_mapping(parent->page_directory,
                               parent->user_stack_base,
                               &parent_phys,
                               &parent_flags) != 0) {
            return -1;
        }

        unsigned int cow_flags = (parent_flags | 0x4u) & ~0x2u;
        if (paging_map_page(child->page_directory,
                            parent->user_stack_base,
                            parent_phys,
                            cow_flags) != 0) {
            return -1;
        }
        if (paging_set_page_writable(parent->page_directory,
                                     parent->user_stack_base,
                                     0) != 0) {
            return -1;
        }

        paging_inc_page_ref(parent_phys & 0xFFFFF000u);
        child->user_stack_base = parent->user_stack_base;
        child->user_stack_size = parent->user_stack_size;
    }

    if (parent->brk_base && parent->brk_limit > parent->brk_base) {
        unsigned int brk_size = parent->brk_limit - parent->brk_base;
        unsigned int brk_pages = align_up_u32(brk_size, 4096u) / 4096u;
        for (unsigned int p = 0; p < brk_pages; p++) {
            unsigned int vaddr = parent->brk_base + p * 4096u;
            if (share_cow_page(parent->page_directory, child->page_directory, vaddr) != 0) {
                return -1;
            }
        }

        child->brk_base = parent->brk_base;
        child->brk_current = parent->brk_current;
        child->brk_limit = parent->brk_limit;
    }

    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!parent->mmap_regions[i].in_use) continue;
        unsigned int pages = parent->mmap_regions[i].size / 4096u;
        for (unsigned int p = 0; p < pages; p++) {
            unsigned int vaddr = parent->mmap_regions[i].addr + p * 4096u;
            if (share_cow_page(parent->page_directory, child->page_directory, vaddr) != 0) {
                return -1;
            }
        }
        child->mmap_regions[i] = parent->mmap_regions[i];
    }

    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!parent->elf_regions[i].in_use) continue;
        unsigned int pages = parent->elf_regions[i].size / 4096u;
        for (unsigned int p = 0; p < pages; p++) {
            unsigned int vaddr = parent->elf_regions[i].addr + p * 4096u;
            if (share_cow_page(parent->page_directory, child->page_directory, vaddr) != 0) {
                return -1;
            }
        }
        child->elf_regions[i] = parent->elf_regions[i];
    }

    return 0;
}

static unsigned int linux_do_brk(unsigned int requested) {
    PCB* proc = current_proc();
    if (!proc) return LINUX_EINVAL;

    if (proc->brk_base == 0) {
        unsigned int pages = 256u;
        unsigned int order = pages_to_order(pages);
        void* block = alloc_pages(order);
        if (!block) return LINUX_ENOMEM;
        proc->brk_base = (unsigned int)(uintptr_t)block;
        proc->brk_current = proc->brk_base;
        proc->brk_limit = proc->brk_base + pages * 4096u;
        if (paging_mark_user_range(proc->page_directory, proc->brk_base, pages * 4096u) != 0) {
            free_pages(block, order);
            proc->brk_base = 0;
            proc->brk_current = 0;
            proc->brk_limit = 0;
            return LINUX_ENOMEM;
        }
    }

    if (requested == 0) return proc->brk_current;
    if (requested < proc->brk_base || requested > proc->brk_limit) return proc->brk_current;

    proc->brk_current = requested;
    return proc->brk_current;
}

static unsigned int linux_do_mmap(unsigned int arg0, unsigned int arg1, unsigned int arg2) {
    (void)arg2;
    PCB* proc = current_proc();
    if (!proc) return LINUX_EINVAL;

    unsigned int len = arg1;
    if (len == 0u && arg0 != 0u) {
        LinuxMmapArgs* args = (LinuxMmapArgs*)(uintptr_t)arg0;
        len = args->len;
    }
    if (len == 0u) return LINUX_EINVAL;

    unsigned int page_len = align_up_u32(len, 4096u);
    unsigned int pages = page_len / 4096u;
    unsigned int order = pages_to_order(pages);
    void* block = alloc_pages(order);
    if (!block) return LINUX_ENOMEM;

    unsigned int block_bytes = (1u << order) * 4096u;
    if (paging_mark_user_range(proc->page_directory, (unsigned int)(uintptr_t)block, block_bytes) != 0) {
        free_pages(block, order);
        return LINUX_ENOMEM;
    }

    int slot = find_free_mmap_slot(proc);
    if (slot < 0) {
        free_pages(block, order);
        return LINUX_ENOMEM;
    }

    proc->mmap_regions[slot].in_use = 1;
    proc->mmap_regions[slot].addr = (unsigned int)(uintptr_t)block;
    proc->mmap_regions[slot].size = block_bytes;
    proc->mmap_regions[slot].order = order;
    return proc->mmap_regions[slot].addr;
}

static unsigned int linux_do_munmap(unsigned int addr, unsigned int len) {
    (void)len;
    PCB* proc = current_proc();
    if (!proc) return LINUX_EINVAL;

    for (int i = 0; i < MAX_PROCESS_MMAP_REGIONS; i++) {
        if (!proc->mmap_regions[i].in_use) continue;
        if (proc->mmap_regions[i].addr != addr) continue;
        unsigned int pages = proc->mmap_regions[i].size / 4096u;
        for (unsigned int p = 0; p < pages; p++) {
            free_page((void*)(proc->mmap_regions[i].addr + p * 4096u));
        }
        proc->mmap_regions[i].in_use = 0;
        proc->mmap_regions[i].addr = 0;
        proc->mmap_regions[i].size = 0;
        proc->mmap_regions[i].order = 0;
        return 0;
    }
    return LINUX_EINVAL;
}

static unsigned int linux_do_fork(IRQContext* ctx) {
    PCB* parent = current_proc();
    if (!parent) return LINUX_EINVAL;
    if (!ctx || (ctx->cs & 3u) != 3u) return LINUX_ENOSYS;

    int child_pid = process_create((unsigned int)process_fork_resume_entry);
    if (child_pid < 0) return LINUX_ENOMEM;

    PCB* child = &process_table[child_pid];
    child->regs[7] = 0;
    child->parent_pid = parent->pid;
    child->pending_signals = 0;
    str_copy(child->name, parent->name, (int)sizeof(child->name));
    str_copy(child->cwd, parent->cwd, (int)sizeof(child->cwd));

    unsigned int new_pd = paging_create_process_directory(0, 0, 0);
    if (!new_pd) {
        process_kill(child_pid);
        return LINUX_ENOMEM;
    }

    paging_destroy_process_directory(child->page_directory);
    child->page_directory = new_pd;

    if (clone_user_mappings(parent, child) != 0) {
        process_kill(child_pid);
        return LINUX_ENOMEM;
    }

    child->fork_context_valid = 1;
    child->fork_edi = ctx->edi;
    child->fork_esi = ctx->esi;
    child->fork_ebp = ctx->ebp;
    child->fork_ebx = ctx->ebx;
    child->fork_edx = ctx->edx;
    child->fork_ecx = ctx->ecx;
    child->fork_eax = 0;
    child->fork_eip = ctx->eip;
    child->fork_cs = ctx->cs;
    child->fork_eflags = ctx->eflags;
    child->fork_user_esp = ctx->user_esp;
    child->fork_user_ss = ctx->user_ss;
    child->eip = (unsigned int)process_fork_resume_entry;

    // Parent gets child PID; child is launched with the same entrypoint model.
    return (unsigned int)child_pid;
}

static unsigned int linux_do_execve(const char* path) {
    PCB* proc = current_proc();
    if (!proc || !path) return LINUX_EFAULT;

    unsigned int new_pd = paging_create_process_directory(0, 0, 0);
    if (!new_pd) return LINUX_ENOMEM;

    if (proc->user_stack_base) {
        unsigned int old_stack_phys = 0;
        unsigned int old_stack_flags = 0;
        if (paging_get_mapping(proc->page_directory,
                               proc->user_stack_base,
                               &old_stack_phys,
                               &old_stack_flags) == 0) {
            (void)old_stack_flags;
            free_page((void*)(old_stack_phys & 0xFFFFF000u));
        } else {
            free_page((void*)proc->user_stack_base);
        }
        proc->user_stack_base = 0;
        proc->user_stack_size = 0;
    }
    release_proc_user_allocations(proc);

    if (proc->page_directory) {
        paging_destroy_process_directory(proc->page_directory);
    }
    proc->page_directory = new_pd;

    void* user_stack = alloc_page();
    if (!user_stack) return LINUX_ENOMEM;
    proc->user_stack_base = (unsigned int)(uintptr_t)user_stack;
    proc->user_stack_size = 4096u;

    if (paging_mark_user_range(proc->page_directory, proc->user_stack_base, proc->user_stack_size) != 0) {
        free_page(user_stack);
        proc->user_stack_base = 0;
        proc->user_stack_size = 0;
        return LINUX_ENOMEM;
    }

    if (elf_load(path, proc) != 0) {
        free_page(user_stack);
        proc->user_stack_base = 0;
        proc->user_stack_size = 0;
        return LINUX_ENOENT;
    }

    proc->regs[7] = 1;
    jump_to_ring3(proc->eip, proc->user_stack_base + proc->user_stack_size - 16u);
    while (1) { }
}

// Shared VGA cursor for Linux-process stdout/stderr output.
// Auto-positioned to the row after the last non-blank line on first use.
static int linux_stdout_cursor = -1;

static void linux_ensure_cursor(void) {
    if (linux_stdout_cursor >= 0) return;
    char* video = (char*)0xB8000;
    linux_stdout_cursor = 0;
    for (int i = 80 * 25 - 1; i >= 0; i--) {
        if (video[i * 2] != ' ' && video[i * 2] != 0) {
            linux_stdout_cursor = ((i / 80) + 1) * 80;
            break;
        }
    }
}

// Write n bytes of buf to the VGA terminal (stdout/stderr).
// linux_ensure_cursor() already positions linux_stdout_cursor at the *start*
// of a fresh line, so we use print_string_sameline (no leading newline advance)
// to avoid skipping an extra blank line on every write.
extern void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);
static void linux_term_write(const char* buf, int n) {
    char* video = (char*)0xB8000;
    linux_ensure_cursor();
    print_string_sameline(buf, n, video, &linux_stdout_cursor, 0x07);
}

unsigned int linux_syscall_dispatch(unsigned int number,
                                    unsigned int arg0,
                                    unsigned int arg1,
                                    unsigned int arg2,
                                    IRQContext* ctx) {
    unsigned int ret;
    process_record_irq_context(ctx);
    switch (number) {

    // ── exit / exit_group ────────────────────────────────────────────────────
    case LINUX_SYS_EXIT:
    case LINUX_SYS_EXIT_GROUP:
        process_exit_with_status(arg0);
        return 0; // not reached

    // ── write ────────────────────────────────────────────────────────────────
    // fd=1 (stdout) and fd=2 (stderr) → VGA terminal
    // other fds → VFS write
    case LINUX_SYS_WRITE: {
        int   fd  = (int)arg0;
        const char* buf = (const char*)arg1;
        int   len = (int)arg2;
        if (!buf || len <= 0) return 0;
        if (fd == 1 || fd == 2) {
            linux_term_write(buf, len);
            return (unsigned int)len;
        }
        LinuxPipeFd* pipe_fd = linux_pipe_fd_get(fd);
        if (pipe_fd) {
            if (!pipe_fd->is_write) return (unsigned int)LINUX_EBADF;
            LinuxPipe* pipe = &linux_pipes[pipe_fd->pipe_index];
            if (pipe->readers == 0u) return (unsigned int)LINUX_EPIPE;
            if (pipe->count == PIPE_DATA_SIZE) {
                process_block_current(PROC_WAIT_PIPE_WRITE, pipe_fd->pipe_index);
            }
            if (pipe->count >= PIPE_DATA_SIZE) return (unsigned int)LINUX_EAGAIN;
            unsigned int capacity = PIPE_DATA_SIZE - pipe->count;
            unsigned int to_write = (unsigned int)len;
            if (to_write > capacity) to_write = capacity;
            for (unsigned int i = 0; i < to_write; i++) {
                pipe->data[pipe->tail] = (unsigned char)buf[i];
                pipe->tail = (pipe->tail + 1u) % PIPE_DATA_SIZE;
            }
            pipe->count += to_write;
            process_wake_waiting_with_arg(PROC_WAIT_PIPE_READ, pipe_fd->pipe_index);
            return (unsigned int)to_write;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_write(fd, buf, len);
        return (r < 0) ? (unsigned int)LINUX_EBADF : (unsigned int)r;
    }

    // ── read ─────────────────────────────────────────────────────────────────
    // fd=0 (stdin) → blocking keyboard read into buf
    // other fds → VFS read
    case LINUX_SYS_READ: {
        int   fd  = (int)arg0;
        char* buf = (char*)arg1;
        int   len = (int)arg2;
        if (!buf || len <= 0) return 0;
        LinuxPipeFd* pipe_fd = linux_pipe_fd_get(fd);
        if (pipe_fd) {
            if (pipe_fd->is_write) return (unsigned int)LINUX_EBADF;
            LinuxPipe* pipe = &linux_pipes[pipe_fd->pipe_index];
            if (pipe->count == 0u) {
                if (pipe->writers == 0u) return 0;
                process_block_current(PROC_WAIT_PIPE_READ, pipe_fd->pipe_index);
            }
            if (pipe->count == 0u) return 0;
            unsigned int to_read = (unsigned int)len;
            if (to_read > pipe->count) to_read = pipe->count;
            for (unsigned int i = 0; i < to_read; i++) {
                buf[i] = (char)pipe->data[pipe->head];
                pipe->head = (pipe->head + 1u) % PIPE_DATA_SIZE;
            }
            pipe->count -= to_read;
            process_wake_waiting_with_arg(PROC_WAIT_PIPE_WRITE, pipe_fd->pipe_index);
            return (unsigned int)to_read;
        }
        if (fd == 0) {
            // Read one line from keyboard. Yield without consuming CPU while
            // the process waits for an IRQ to add a scancode.
            int n = 0;
            while (n < len - 1) {
                unsigned char sc;
                while (!keyboard_pop_scancode(&sc)) {
                    process_block_current(PROC_WAIT_KEYBOARD, 0u);
                }
                if (sc & 0x80) continue;                    // key release
                char c = scancode_to_char(sc, 0);
                if (!c) continue;
                if (c == '\n') { buf[n++] = '\n'; break; }
                if (c == 8 && n > 0) { n--; continue; }    // backspace
                if (c >= 32 && c <= 126) buf[n++] = c;
            }
            buf[n] = 0;
            return (unsigned int)n;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_read(fd, buf, len);
        return (r < 0) ? (unsigned int)LINUX_EBADF : (unsigned int)r;
    }

    // ── open ─────────────────────────────────────────────────────────────────
    case LINUX_SYS_OPEN: {
        const char* path  = (const char*)arg0;
        int         flags = (int)arg1;
        char resolved[MAX_PATH_LENGTH];
        if (linux_resolve_path(path, resolved, (unsigned int)sizeof(resolved)) != 0) {
            return (unsigned int)LINUX_ENOENT;
        }
        // Map Linux open flags to our FS flags
        int our_flags = 0;
        if ((flags & 3) == 0)         our_flags |= FS_O_READ;
        if ((flags & 3) == 1)         our_flags |= FS_O_WRITE;
        if ((flags & 3) == 2)         our_flags |= FS_O_READ | FS_O_WRITE;
        if (flags & 0x40 /*O_CREAT*/) our_flags |= FS_O_CREATE;
        if (flags & 0x200/*O_TRUNC*/) our_flags |= FS_O_TRUNC;
        if (flags & 0x400/*O_APPEND*/) our_flags |= FS_O_APPEND;
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int fd = vfs_open(resolved, our_flags);
        return (fd < 0) ? (unsigned int)LINUX_ENOENT : (unsigned int)fd;
    }

    // ── close ────────────────────────────────────────────────────────────────
    case LINUX_SYS_CLOSE: {
        LinuxPipeFd* pipe_fd = linux_pipe_fd_get((int)arg0);
        if (pipe_fd) {
            LinuxPipe* pipe = &linux_pipes[pipe_fd->pipe_index];
            if (pipe_fd->is_write && pipe->writers > 0u) pipe->writers--;
            if (!pipe_fd->is_write && pipe->readers > 0u) pipe->readers--;
            pipe_fd->used = 0;
            if (pipe->readers == 0u && pipe->writers == 0u) {
                pipe->used = 0;
                pipe->count = 0;
                pipe->head = 0;
                pipe->tail = 0;
            }
            process_wake_waiting(PROC_WAIT_PIPE_READ);
            process_wake_waiting(PROC_WAIT_PIPE_WRITE);
            return 0;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_close((int)arg0);
        return (r < 0) ? (unsigned int)LINUX_EBADF : 0;
    }

    // ── pipe ─────────────────────────────────────────────────────────────────
    case LINUX_SYS_PIPE: {
        int* fds = (int*)arg0;
        if (!fds) return (unsigned int)LINUX_EFAULT;
        int pipe_index = -1;
        for (int i = 0; i < MAX_PIPES; i++) {
            if (linux_pipes[i].used) continue;
            pipe_index = i;
            linux_pipes[i].head = 0;
            linux_pipes[i].tail = 0;
            linux_pipes[i].count = 0;
            linux_pipes[i].readers = 0;
            linux_pipes[i].writers = 0;
            linux_pipes[i].used = 1;
            break;
        }
        if (pipe_index < 0) return (unsigned int)LINUX_EMFILE;
        int read_fd = linux_pipe_fd_alloc((unsigned int)pipe_index, 0u);
        int write_fd = linux_pipe_fd_alloc((unsigned int)pipe_index, 1u);
        if (read_fd < 0 || write_fd < 0) {
            linux_pipes[pipe_index].used = 0;
            return (unsigned int)LINUX_EMFILE;
        }
        fds[0] = read_fd;
        fds[1] = write_fd;
        return 0;
    }

    // ── poll ─────────────────────────────────────────────────────────────────
    case LINUX_SYS_POLL: {
        typedef struct {
            int fd;
            unsigned short events;
            unsigned short revents;
        } LinuxPollFd;

        LinuxPollFd* polls = (LinuxPollFd*)arg0;
        unsigned int nfds = arg1;
        int timeout = (int)arg2;
        if (!polls && nfds != 0u) return (unsigned int)LINUX_EFAULT;

poll_again:
        unsigned int ready_count = 0u;
        for (unsigned int i = 0; i < nfds; i++) {
            LinuxPollFd* p = &polls[i];
            unsigned short revents = 0;
            if (p->fd == 0) {
                if (keyboard_available() && (p->events & LINUX_POLLIN)) revents |= LINUX_POLLIN;
            } else {
                LinuxPipeFd* pipe_fd = linux_pipe_fd_get(p->fd);
                if (pipe_fd) {
                    LinuxPipe* pipe = &linux_pipes[pipe_fd->pipe_index];
                    if (!pipe_fd->is_write && pipe->count > 0u &&
                        (p->events & LINUX_POLLIN)) revents |= LINUX_POLLIN;
                    if (!pipe_fd->is_write && pipe->writers == 0u) revents |= LINUX_POLLHUP;
                    if (pipe_fd->is_write && pipe->count < PIPE_DATA_SIZE &&
                        pipe->readers > 0u && (p->events & LINUX_POLLOUT)) revents |= LINUX_POLLOUT;
                    if (pipe_fd->is_write && pipe->readers == 0u) revents |= LINUX_POLLERR;
                } else if (p->fd >= 0) {
                    if (p->events & LINUX_POLLIN) revents |= LINUX_POLLIN;
                    if (p->events & LINUX_POLLOUT) revents |= LINUX_POLLOUT;
                }
            }
            p->revents = revents;
            if (revents) ready_count++;
        }
        if (ready_count != 0u || timeout == 0) return ready_count;

        if (timeout > 0) {
            unsigned int timeout_ticks = (unsigned int)timeout * 18u / 1000u;
            if (timeout_ticks == 0u) timeout_ticks = 1u;
            unsigned int wake_at = (unsigned int)ticks + timeout_ticks;
            process_block_current(PROC_WAIT_TIMER, wake_at);
            return 0;
        } else {
            unsigned int wait_pipe = 0u;
            unsigned int wait_reason = PROC_WAIT_KEYBOARD;
            for (unsigned int i = 0; i < nfds; i++) {
                LinuxPipeFd* pipe_fd = linux_pipe_fd_get(polls[i].fd);
                if (pipe_fd) {
                    wait_pipe = pipe_fd->pipe_index;
                    wait_reason = pipe_fd->is_write ? PROC_WAIT_PIPE_WRITE : PROC_WAIT_PIPE_READ;
                    break;
                }
            }
            process_block_current(wait_reason, wait_pipe);
            goto poll_again;
        }

        return ready_count;
    }

    // ── chdir / getcwd ────────────────────────────────────────────────────────
    case LINUX_SYS_CHDIR: {
        const char* path = (const char*)arg0;
        if (!path) return (unsigned int)LINUX_EFAULT;
        char resolved[MAX_PATH_LENGTH];
        if (linux_resolve_path(path, resolved, (unsigned int)sizeof(resolved)) != 0) {
            return (unsigned int)LINUX_ENOENT;
        }
        int r = process_set_cwd(resolved);
        if (r < 0) return (unsigned int)LINUX_ENOENT;
        return 0;
    }

    case LINUX_SYS_GETCWD: {
        char* buf = (char*)arg0;
        unsigned int size = arg1;
        if (!buf || size == 0) return (unsigned int)LINUX_EFAULT;
        int len = process_get_cwd(buf, size);
        if (len == -2) return (unsigned int)LINUX_ERANGE;
        if (len < 0) return (unsigned int)LINUX_EFAULT;
        if ((unsigned int)len + 1u > size) {
            buf[0] = 0;
            return (unsigned int)LINUX_ERANGE;
        }
        return (unsigned int)buf;
    }

    // ── lseek / stat / fstat ──────────────────────────────────────────────────
    case LINUX_SYS_LSEEK:
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        ret = (unsigned int)vfs_seek((int)arg0, (int)arg1, (int)arg2);
        break;

    case LINUX_SYS_STAT: {
        const char* path = (const char*)arg0;
        FInode* stat_out = (FInode*)arg1;
        if (!path || !stat_out) return (unsigned int)LINUX_EFAULT;
        char resolved[MAX_PATH_LENGTH];
        if (linux_resolve_path(path, resolved, (unsigned int)sizeof(resolved)) != 0) {
            return (unsigned int)LINUX_ENOENT;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_stat(resolved, stat_out);
        return (r < 0) ? (unsigned int)LINUX_ENOENT : 0;
    }

    case LINUX_SYS_FSTAT: {
        FInode* stat_out = (FInode*)arg1;
        if (!stat_out) return (unsigned int)LINUX_EFAULT;
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_fstat((int)arg0, stat_out);
        return (r < 0) ? (unsigned int)LINUX_EBADF : 0;
    }

    // ── unlink / mkdir / rmdir ───────────────────────────────────────────────
    case LINUX_SYS_UNLINK: {
        const char* path = (const char*)arg0;
        char resolved[MAX_PATH_LENGTH];
        if (!path || linux_resolve_path(path, resolved, (unsigned int)sizeof(resolved)) != 0) {
            return (unsigned int)LINUX_EFAULT;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_unlink(resolved);
        return (r < 0) ? (unsigned int)LINUX_ENOENT : 0;
    }

    case LINUX_SYS_MKDIR: {
        const char* path = (const char*)arg0;
        char resolved[MAX_PATH_LENGTH];
        if (!path || linux_resolve_path(path, resolved, (unsigned int)sizeof(resolved)) != 0) {
            return (unsigned int)LINUX_EFAULT;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_mkdir(resolved);
        return (r < 0) ? (unsigned int)LINUX_EEXIST : 0;
    }

    case LINUX_SYS_RMDIR: {
        const char* path = (const char*)arg0;
        char resolved[MAX_PATH_LENGTH];
        if (!path || linux_resolve_path(path, resolved, (unsigned int)sizeof(resolved)) != 0) {
            return (unsigned int)LINUX_EFAULT;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_unlink(resolved);
        return (r < 0) ? (unsigned int)LINUX_ENOENT : 0;
    }

    // ── rename / fsync ───────────────────────────────────────────────────────
    case LINUX_SYS_RENAME: {
        const char* old_path = (const char*)arg0;
        const char* new_path = (const char*)arg1;
        char old_resolved[MAX_PATH_LENGTH];
        char new_resolved[MAX_PATH_LENGTH];
        if (!old_path || !new_path ||
            linux_resolve_path(old_path, old_resolved, (unsigned int)sizeof(old_resolved)) != 0 ||
            linux_resolve_path(new_path, new_resolved, (unsigned int)sizeof(new_resolved)) != 0) {
            return (unsigned int)LINUX_EFAULT;
        }
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int r = vfs_rename(old_resolved, new_resolved);
        return (r < 0) ? (unsigned int)LINUX_ENOENT : 0;
    }

    case LINUX_SYS_FSYNC: {
        if (!ensure_newfs_ready()) return (unsigned int)LINUX_ENOSYS;
        int fd = (int)arg0;
        if (fd == 1 || fd == 2) return 0;
        LinuxPipeFd* pipe_fd = linux_pipe_fd_get(fd);
        if (pipe_fd) return (unsigned int)LINUX_EINVAL;
        if (disk_fs_flush() != 0) return (unsigned int)LINUX_EIO;
        return 0;
    }

    // ── waitpid ──────────────────────────────────────────────────────────────
    case LINUX_SYS_WAITPID: {
        int status_pid = process_waitpid((int)arg0,
                                         (unsigned int*)arg1,
                                         (int)arg2);
        if (status_pid < 0) return (unsigned int)status_pid;
        return (unsigned int)status_pid;
    }

    // ── getpid ───────────────────────────────────────────────────────────────
    case LINUX_SYS_GETPID:
        if (current_process < 0) return 1;
        return (unsigned int)current_process;

    // ── getuid / getgid / geteuid / getegid ─ always "root" for now ─────────
    case LINUX_SYS_GETUID:
    case LINUX_SYS_GETGID:
    case LINUX_SYS_GETEUID:
    case LINUX_SYS_GETEGID:
        return 0;

    // ── brk ──────────────────────────────────────────────────────────────────
    case LINUX_SYS_BRK:
        ret = linux_do_brk(arg0);
        break;

    // ── fork / execve ───────────────────────────────────────────────────────
    case LINUX_SYS_FORK:
        ret = linux_do_fork(ctx);
        break;

    case LINUX_SYS_EXECVE:
        ret = linux_do_execve((const char*)arg0);
        break;

    // ── uname ────────────────────────────────────────────────────────────────
    case LINUX_SYS_UNAME: {
        LinuxUtsname* u = (LinuxUtsname*)arg0;
        if (!u) return (unsigned int)LINUX_EFAULT;
        sc_memset(u, 0, sizeof(LinuxUtsname));
        sc_memcpy(u->sysname,  "Smiggles",  9);
        sc_memcpy(u->nodename, "smiggles",  9);
        sc_memcpy(u->release,  "1.0.0",     6);
        sc_memcpy(u->version,  "#1",        3);
        sc_memcpy(u->machine,  "i686",      5);
        return 0;
    }

    // ── mmap / munmap ────────────────────────────────────────────────────────
    case LINUX_SYS_MMAP:
        ret = linux_do_mmap(arg0, arg1, arg2);
        break;

    case LINUX_SYS_MUNMAP:
        ret = linux_do_munmap(arg0, arg1);
        break;

    // ── ioctl ─ stub ─────────────────────────────────────────────────────────
    case LINUX_SYS_IOCTL:
        ret = (unsigned int)LINUX_EINVAL;
        break;

    default:
        ret = (unsigned int)LINUX_ENOSYS;
        break;
    }

    process_deliver_pending_signals(ctx);
    return ret;
}

unsigned int syscall_dispatch(unsigned int number,
                              unsigned int arg0,
                              unsigned int arg1,
                              unsigned int arg2,
                              unsigned int saved_cs,
                              IRQContext* ctx) {
    process_record_irq_context(ctx);
    // Ring-3 caller → Linux ABI
    if ((saved_cs & 3) == 3)
        return linux_syscall_dispatch(number, arg0, arg1, arg2, ctx);

    unsigned int ret;
    switch (number) {
        case SYS_YIELD:
            process_yield();
            ret = 0;
            break;
        case SYS_GET_TICKS:
            ret = (unsigned int)ticks;
            break;
        case SYS_GET_PID:
            if (current_process < 0) ret = 0xFFFFFFFFu;
            else ret = (unsigned int)current_process;
            break;
        case SYS_WAIT_TICKS: {
            if (arg0 == 0u) {
                ret = (unsigned int)ticks;
            } else if (current_process < 0) {
                unsigned int start = (unsigned int)ticks;
                while (((unsigned int)ticks - start) < arg0) {
                    asm volatile("hlt");
                }
                ret = (unsigned int)ticks;
            } else {
                unsigned int wake_at = (unsigned int)ticks + arg0;
                process_block_current(PROC_WAIT_TIMER, wake_at);
                ret = (unsigned int)ticks;
            }
            break;
        }
        case SYS_SPAWN_DEMO:
            ret = (unsigned int)process_spawn_demo_with_work(arg0);
            break;
        case SYS_KILL_PID:
            ret = (unsigned int)process_kill((int)arg0);
            break;
        case SYS_GET_CPL:
            ret = protection_get_cpl();
            break;
        case SYS_OPEN:
            if (!ensure_newfs_ready()) ret = 0xFFFFFFFFu;
            else ret = (unsigned int)vfs_open((const char*)arg0, (int)arg1);
            break;
        case SYS_CLOSE:
            if (!ensure_newfs_ready()) ret = 0xFFFFFFFFu;
            else ret = (unsigned int)vfs_close((int)arg0);
            break;
        case SYS_READ:
            if (!ensure_newfs_ready()) ret = 0xFFFFFFFFu;
            else ret = (unsigned int)vfs_read((int)arg0, (char*)arg1, (int)arg2);
            break;
        case SYS_WRITE:
            if (!ensure_newfs_ready()) ret = 0xFFFFFFFFu;
            else ret = (unsigned int)vfs_write((int)arg0, (const char*)arg1, (int)arg2);
            break;
        default:
            ret = 0xFFFFFFFFu;
            break;
    }

    process_deliver_pending_signals(ctx);
    return ret;
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
