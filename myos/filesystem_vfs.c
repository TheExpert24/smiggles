// ============================================================================
// PHASE 5: VFS Abstraction Layer
// ============================================================================

#include "kernel.h"
#include "filesystem_new.h"
#include <stdint.h>

#define UNUSED(x) (void)(x)

// External file I/O functions
extern int fs_fd_open(const char* path, int flags);
extern int fs_fd_close(int fd);
extern int fs_fd_read(int fd, char* buffer, int count);
extern int fs_fd_write(int fd, const char* buffer, int count);
extern int fs_fd_seek(int fd, uint32_t offset, int whence);
extern int newfs_readdir(const char* path, DirectoryEntry* entries, int max_entries);
extern int newfs_mkdir(const char* path);
extern int newfs_unlink(const char* path);
extern int newfs_stat(const char* path, FInode* stat_out);

extern int disk_fs_init(void);

// Utility functions
static void* my_memcpy(void* dest, const void* src, int n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static int my_strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

static int my_strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int my_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || !b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

static int path_basename(const char* path, char* out, int out_len) {
    if (!path || !out || out_len <= 1) return 0;

    const char* start = path;
    const char* p = path;
    while (*p) {
        if (*p == '/') start = p + 1;
        p++;
    }

    if (*start == 0) return 0;

    int i = 0;
    while (start[i] && start[i] != '/' && i < out_len - 1) {
        out[i] = start[i];
        i++;
    }
    out[i] = 0;
    return i > 0;
}

// ============================================================================
// VFS Mount Table
// ============================================================================

#define MAX_VFS_MOUNTS 16
static VFS_Mount vfs_mounts[MAX_VFS_MOUNTS];
static int vfs_mount_count = 0;

#define MAX_VFS_OPEN_FILES 256
typedef struct {
    int used;
    int mount_idx;
    int backend_fd;
    int owner_pid;
} VFS_OpenFile;

static VFS_OpenFile vfs_open_files[MAX_VFS_OPEN_FILES];

static int vfs_alloc_fd(int mount_idx, int backend_fd) {
    extern int current_process;
    for (int i = 0; i < MAX_VFS_OPEN_FILES; i++) {
        if (!vfs_open_files[i].used) {
            vfs_open_files[i].used = 1;
            vfs_open_files[i].mount_idx = mount_idx;
            vfs_open_files[i].backend_fd = backend_fd;
            vfs_open_files[i].owner_pid = (current_process >= 0) ? process_table[current_process].pid : -1;
            return i;
        }
    }
    return -1;
}

static int vfs_fd_valid(int fd) {
    return fd >= 0 && fd < MAX_VFS_OPEN_FILES && vfs_open_files[fd].used;
}

static void vfs_fd_reset(int fd) {
    vfs_open_files[fd].used = 0;
    vfs_open_files[fd].mount_idx = -1;
    vfs_open_files[fd].backend_fd = -1;
    vfs_open_files[fd].owner_pid = -1;
}

// ============================================================================
// VFS Helper Functions
// ============================================================================

// Find the mount point for a given path
// Returns mount index or -1 if none found
static int vfs_find_mount(const char* path) {
    if (!path) return -1;
    
    int best_match = -1;
    int best_match_len = 0;
    
    for (int i = 0; i < vfs_mount_count; i++) {
        if (!vfs_mounts[i].used) continue;
        
        int mount_len = my_strlen(vfs_mounts[i].mount_point);
        
        // Check if path starts with this mount point
        if (my_strncmp(path, vfs_mounts[i].mount_point, mount_len) == 0) {
            // Make sure it's a proper match (full path component)
            if (path[mount_len] == 0 || path[mount_len] == '/' || 
                (mount_len > 0 && vfs_mounts[i].mount_point[mount_len - 1] == '/')) {
                
                if (mount_len > best_match_len) {
                    best_match = i;
                    best_match_len = mount_len;
                }
            }
        }
    }
    
    return best_match;
}

// Convert absolute path to relative path for a specific mount
// e.g., /proc/uptime with mount /proc -> uptime
static void vfs_make_relative_path(const char* path, const char* mount_point, char* rel_path_out) {
    int mount_len = my_strlen(mount_point);
    
    if (my_strcmp(path, mount_point) == 0 || my_strcmp(path, "/") == 0) {
        rel_path_out[0] = '/';
        rel_path_out[1] = 0;
        return;
    }
    
    // Skip mount point prefix
    const char* rel = path + mount_len;
    if (*rel == '/') {
        rel++;
    }
    
    rel_path_out[0] = '/';
    int i = 1;
    while (*rel && i < 255) {
        rel_path_out[i++] = *rel++;
    }
    rel_path_out[i] = 0;
}

// ============================================================================
// Default Disk-Based VFS Operations
// ============================================================================

static int vfs_disk_open(const char* path, int flags) {
    return fs_fd_open(path, flags);
}

static int vfs_disk_close(int fd) {
    return fs_fd_close(fd);
}

static int vfs_disk_read(int fd, char* buf, int count) {
    return fs_fd_read(fd, buf, count);
}

static int vfs_disk_write(int fd, const char* buf, int count) {
    return fs_fd_write(fd, buf, count);
}

static int vfs_disk_readdir(int fd, DirectoryEntry* entries, int max_entries) {
    UNUSED(fd);
    return newfs_readdir("/", entries, max_entries);
}

static int vfs_disk_mkdir(const char* path) {
    return newfs_mkdir(path);
}

static int vfs_disk_unlink(const char* path) {
    return newfs_unlink(path);
}

static VFS_Operations vfs_disk_ops = {
    .open = vfs_disk_open,
    .close = vfs_disk_close,
    .read = vfs_disk_read,
    .write = vfs_disk_write,
    .readdir = vfs_disk_readdir,
    .mkdir = vfs_disk_mkdir,
    .unlink = vfs_disk_unlink
};

// ============================================================================
// /proc Virtual Filesystem Backend
// ============================================================================

typedef struct {
    char name[32];
    const char* (*get_content)(void);  // Function to get file content
} ProcFile;

typedef struct {
    int used;
    int file_idx;
    int offset;
} ProcOpenFile;

#define PROC_MAX_OPEN_FILES 16
static ProcOpenFile proc_open_files[PROC_MAX_OPEN_FILES];

static const char* proc_uptime_content(void) {
    extern volatile int ticks;
    static char buffer[64];
    char whole[16];
    char frac[4];
    int seconds = ticks / 18;
    int rem = ticks % 18;
    int centi = (rem * 100) / 18;

    int_to_str(seconds, whole);
    frac[0] = (char)('0' + ((centi / 10) % 10));
    frac[1] = (char)('0' + (centi % 10));
    frac[2] = 0;

    buffer[0] = 0;
    str_concat(buffer, whole);
    str_concat(buffer, ".");
    str_concat(buffer, frac);
    str_concat(buffer, "\n");

    return buffer;
}

static const char* proc_ticks_content(void) {
    extern volatile int ticks;
    static char buffer[32];
    char tmp[16];
    int_to_str((int)ticks, tmp);
    buffer[0] = 0;
    str_concat(buffer, tmp);
    str_concat(buffer, "\n");
    return buffer;
}

static const char* proc_mounts_content(void) {
    static char buffer[128];
    buffer[0] = 0;
    str_concat(buffer, "/\n");
    str_concat(buffer, "/proc\n");
    str_concat(buffer, "/dev\n");
    str_concat(buffer, "/tmp\n");
    return buffer;
}

static ProcFile proc_files[] __attribute__((unused)) = {
    { "uptime", proc_uptime_content },
    { "ticks", proc_ticks_content },
    { "mounts", proc_mounts_content },
};

static int vfs_proc_open(const char* path, int flags) {
    UNUSED(flags);
    if (!path) return -1;

    if (my_strcmp(path, "/") == 0) return -2;

    char base[32];
    if (!path_basename(path, base, (int)sizeof(base))) return -3;

    int file_idx = -1;
    int proc_count = (int)(sizeof(proc_files) / sizeof(proc_files[0]));
    for (int i = 0; i < proc_count; i++) {
        if (my_strcmp(base, proc_files[i].name) == 0) {
            file_idx = i;
            break;
        }
    }
    if (file_idx < 0) return -4;

    for (int i = 0; i < PROC_MAX_OPEN_FILES; i++) {
        if (!proc_open_files[i].used) {
            proc_open_files[i].used = 1;
            proc_open_files[i].file_idx = file_idx;
            proc_open_files[i].offset = 0;
            return i;
        }
    }

    return -5;
}

static int vfs_proc_close(int fd) {
    if (fd < 0 || fd >= PROC_MAX_OPEN_FILES) return -1;
    if (!proc_open_files[fd].used) return -2;
    proc_open_files[fd].used = 0;
    proc_open_files[fd].file_idx = -1;
    proc_open_files[fd].offset = 0;
    return 0;
}

static int vfs_proc_read(int fd, char* buf, int count) {
    if (fd < 0 || fd >= PROC_MAX_OPEN_FILES || !buf || count < 0) return -1;
    if (!proc_open_files[fd].used) return -2;

    int idx = proc_open_files[fd].file_idx;
    if (idx < 0 || idx >= (int)(sizeof(proc_files) / sizeof(proc_files[0]))) return -3;

    const char* content = proc_files[idx].get_content();
    if (!content) return 0;

    int len = my_strlen(content);
    int off = proc_open_files[fd].offset;
    if (off >= len) return 0;

    int to_copy = len - off;
    if (to_copy > count) to_copy = count;
    my_memcpy(buf, content + off, to_copy);
    proc_open_files[fd].offset += to_copy;
    return to_copy;
}

static int vfs_proc_write(int fd, const char* buf, int count) {
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);
    return -1;  // /proc is read-only
}

static VFS_Operations vfs_proc_ops = {
    .open = vfs_proc_open,
    .close = vfs_proc_close,
    .read = vfs_proc_read,
    .write = vfs_proc_write,
    .readdir = 0,
    .mkdir = 0,
    .unlink = 0
};

// ============================================================================
// /dev Virtual Filesystem Backend
// ============================================================================

// Simple device list
typedef struct {
    char name[32];
    int device_id;
} DeviceFile;

typedef struct {
    int used;
    int device_id;
} DevOpenFile;

#define DEV_MAX_OPEN_FILES 16
static DevOpenFile dev_open_files[DEV_MAX_OPEN_FILES];

enum {
    DEV_ID_NULL = 0,
    DEV_ID_ZERO = 1,
    DEV_ID_RANDOM = 2,
    DEV_ID_STDIN = 3,
    DEV_ID_STDOUT = 4,
    DEV_ID_STDERR = 5,
};

static uint32_t dev_rand_state = 0x12345678u;
static int dev_term_cursor = -1;

static void dev_ensure_cursor(void) {
    if (dev_term_cursor >= 0) return;
    char* video = (char*)0xB8000;
    dev_term_cursor = 0;
    for (int i = 80 * 25 - 1; i >= 0; i--) {
        if (video[i * 2] != ' ' && video[i * 2] != 0) {
            dev_term_cursor = ((i / 80) + 1) * 80;
            break;
        }
    }
}

static void dev_term_write(const char* buf, int count) {
    char* video = (char*)0xB8000;
    dev_ensure_cursor();
    print_string_sameline(buf, count, video, &dev_term_cursor, COLOR_LIGHT_GRAY);
}

static DeviceFile devices[] __attribute__((unused)) = {
    { "null",   0 },
    { "zero",   1 },
    { "random", 2 },
    { "stdin",  3 },
    { "stdout", 4 },
    { "stderr", 5 },
};

static int vfs_dev_open(const char* path, int flags) {
    UNUSED(flags);
    if (!path) return -1;

    if (my_strcmp(path, "/") == 0) return -2;

    char base[32];
    if (!path_basename(path, base, (int)sizeof(base))) return -3;

    int dev_id = -1;
    int dev_count = (int)(sizeof(devices) / sizeof(devices[0]));
    for (int i = 0; i < dev_count; i++) {
        if (my_strcmp(base, devices[i].name) == 0) {
            dev_id = devices[i].device_id;
            break;
        }
    }
    if (dev_id < 0) return -4;

    for (int i = 0; i < DEV_MAX_OPEN_FILES; i++) {
        if (!dev_open_files[i].used) {
            dev_open_files[i].used = 1;
            dev_open_files[i].device_id = dev_id;
            return i;
        }
    }

    return -5;
}

static int vfs_dev_close(int fd) {
    if (fd < 0 || fd >= DEV_MAX_OPEN_FILES) return -1;
    if (!dev_open_files[fd].used) return -2;
    dev_open_files[fd].used = 0;
    dev_open_files[fd].device_id = -1;
    return 0;
}

static int vfs_dev_read(int fd, char* buf, int count) {
    if (fd < 0 || fd >= DEV_MAX_OPEN_FILES || !buf || count < 0) return -1;
    if (!dev_open_files[fd].used) return -2;

    int dev_id = dev_open_files[fd].device_id;
    if (count == 0) return 0;

    if (dev_id == DEV_ID_NULL) return 0;

    if (dev_id == DEV_ID_ZERO) {
        for (int i = 0; i < count; i++) buf[i] = 0;
        return count;
    }

    if (dev_id == DEV_ID_RANDOM) {
        for (int i = 0; i < count; i++) {
            dev_rand_state = dev_rand_state * 1664525u + 1013904223u + (uint32_t)ticks;
            buf[i] = (char)((dev_rand_state >> 24) & 0xFF);
        }
        return count;
    }

    if (dev_id == DEV_ID_STDIN) {
        int n = 0;
        int shift = 0;
        while (n < count) {
            unsigned char sc = 0;
            while (!keyboard_pop_scancode(&sc)) {
            }

            if (sc == 0x2A || sc == 0x36) {
                shift = 1;
                continue;
            }
            if (sc == 0xAA || sc == 0xB6) {
                shift = 0;
                continue;
            }
            if (sc & 0x80) continue;

            char c = scancode_to_char(sc, shift);
            if (!c) continue;

            buf[n++] = c;
            if (c == '\n') break;
        }
        return n;
    }

    return 0;
}

static int vfs_dev_write(int fd, const char* buf, int count) {
    if (fd < 0 || fd >= DEV_MAX_OPEN_FILES || !buf || count < 0) return -1;
    if (!dev_open_files[fd].used) return -2;

    int dev_id = dev_open_files[fd].device_id;

    if (dev_id == DEV_ID_NULL) return count;
    if (dev_id == DEV_ID_ZERO || dev_id == DEV_ID_RANDOM || dev_id == DEV_ID_STDIN) return -3;

    if (dev_id == DEV_ID_STDOUT || dev_id == DEV_ID_STDERR) {
        dev_term_write(buf, count);
        return count;
    }

    return -4;
}

static VFS_Operations vfs_dev_ops = {
    .open = vfs_dev_open,
    .close = vfs_dev_close,
    .read = vfs_dev_read,
    .write = vfs_dev_write,
    .readdir = 0,
    .mkdir = 0,
    .unlink = 0
};

// ============================================================================
// /tmp (Temporary) Filesystem Backend
// ============================================================================

// Can use a simpler RAM-based filesystem for /tmp
static int vfs_tmp_make_abs_path(const char* rel_path, char* out_path, int out_max) {
    if (!rel_path || !out_path || out_max <= 5) return 0;

    if (my_strcmp(rel_path, "/") == 0) return 0;

    out_path[0] = '/';
    out_path[1] = 't';
    out_path[2] = 'm';
    out_path[3] = 'p';
    int pos = 4;

    const char* src = rel_path;
    if (src[0] != '/') {
        if (pos >= out_max - 1) return 0;
        out_path[pos++] = '/';
    }

    while (*src && pos < out_max - 1) {
        out_path[pos++] = *src++;
    }

    if (*src != 0) return 0;
    out_path[pos] = 0;
    return 1;
}

static int vfs_tmp_open(const char* path, int flags) {
    char abs_path[256];
    if (!vfs_tmp_make_abs_path(path, abs_path, (int)sizeof(abs_path))) return -1;
    return fs_fd_open(abs_path, flags);
}

static int vfs_tmp_close(int fd) {
    return fs_fd_close(fd);
}

static int vfs_tmp_read(int fd, char* buf, int count) {
    return fs_fd_read(fd, buf, count);
}

static int vfs_tmp_write(int fd, const char* buf, int count) {
    return fs_fd_write(fd, buf, count);
}

static int vfs_tmp_mkdir(const char* path) {
    char abs_path[256];
    if (!vfs_tmp_make_abs_path(path, abs_path, (int)sizeof(abs_path))) return -1;
    return newfs_mkdir(abs_path);
}

static int vfs_tmp_unlink(const char* path) {
    char abs_path[256];
    if (!vfs_tmp_make_abs_path(path, abs_path, (int)sizeof(abs_path))) return -1;
    return newfs_unlink(abs_path);
}

static VFS_Operations vfs_tmp_ops = {
    .open = vfs_tmp_open,
    .close = vfs_tmp_close,
    .read = vfs_tmp_read,
    .write = vfs_tmp_write,
    .readdir = 0,
    .mkdir = vfs_tmp_mkdir,
    .unlink = vfs_tmp_unlink
};

// ============================================================================
// VFS Public API
// ============================================================================

int vfs_init(void) {
    // Initialize mount table
    for (int i = 0; i < MAX_VFS_MOUNTS; i++) {
        vfs_mounts[i].used = 0;
    }
    vfs_mount_count = 0;

    for (int i = 0; i < MAX_VFS_OPEN_FILES; i++) {
        vfs_fd_reset(i);
    }
    for (int i = 0; i < PROC_MAX_OPEN_FILES; i++) {
        proc_open_files[i].used = 0;
        proc_open_files[i].file_idx = -1;
        proc_open_files[i].offset = 0;
    }
    for (int i = 0; i < DEV_MAX_OPEN_FILES; i++) {
        dev_open_files[i].used = 0;
        dev_open_files[i].device_id = -1;
    }
    
    // Initialize underlying disk filesystem
    if (disk_fs_init() != 0) {
        return -1;
    }
    
    // Mount root (/) on disk
    if (vfs_mount("/", &vfs_disk_ops) != 0) {
        return -1;
    }
    
    // Mount special filesystems
    vfs_mount("/proc", &vfs_proc_ops);
    vfs_mount("/dev", &vfs_dev_ops);
    vfs_mount("/tmp", &vfs_tmp_ops);
    
    return 0;
}

int vfs_mount(const char* path, VFS_Operations* ops) {
    if (!path || !ops) return -1;
    
    if (vfs_mount_count >= MAX_VFS_MOUNTS) {
        return -2;  // Mount table full
    }
    
    // Check if already mounted
    for (int i = 0; i < vfs_mount_count; i++) {
        if (vfs_mounts[i].used && my_strcmp(vfs_mounts[i].mount_point, path) == 0) {
            return -3;  // Already mounted
        }
    }
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_VFS_MOUNTS; i++) {
        if (!vfs_mounts[i].used) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        return -4;  // No free slots
    }
    
    // Initialize mount entry
    vfs_mounts[slot].used = 1;
    int path_len = my_strlen(path);
    if (path_len >= 64) {
        return -5;  // Path too long
    }
    my_memcpy(vfs_mounts[slot].mount_point, path, path_len + 1);
    vfs_mounts[slot].ops = ops;
    vfs_mounts[slot].private_data = 0;
    
    if (slot >= vfs_mount_count) {
        vfs_mount_count = slot + 1;
    }
    
    return 0;
}

int vfs_open(const char* path, int flags) {
    if (!path) return -1;

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) return -1;
    
    // Find the mount for this path
    int mount_idx = vfs_find_mount(normalized);
    if (mount_idx < 0) {
        return -2;  // No mount found
    }
    
    VFS_Mount* mount = &vfs_mounts[mount_idx];
    if (!mount->ops || !mount->ops->open) {
        return -3;  // Mount doesn't support open
    }
    
    // Convert path to relative path for the mount
    char relative_path[256];
    vfs_make_relative_path(normalized, mount->mount_point, relative_path);
    
    int backend_fd = mount->ops->open(relative_path, flags);
    if (backend_fd < 0) return backend_fd;

    int vfd = vfs_alloc_fd(mount_idx, backend_fd);
    if (vfd < 0) {
        if (mount->ops->close) {
            mount->ops->close(backend_fd);
        }
        return -4;
    }

    return vfd;
}

int vfs_close(int fd) {
    if (!vfs_fd_valid(fd)) return -1;

    int mount_idx = vfs_open_files[fd].mount_idx;
    int backend_fd = vfs_open_files[fd].backend_fd;
    if (mount_idx < 0 || mount_idx >= MAX_VFS_MOUNTS || !vfs_mounts[mount_idx].used) {
        vfs_fd_reset(fd);
        return -2;
    }

    VFS_Operations* ops = vfs_mounts[mount_idx].ops;
    if (!ops || !ops->close) {
        vfs_fd_reset(fd);
        return -3;
    }

    int ret = ops->close(backend_fd);
    vfs_fd_reset(fd);
    return ret;
}

int vfs_read(int fd, char* buf, int count) {
    if (!vfs_fd_valid(fd) || !buf) return -1;

    int mount_idx = vfs_open_files[fd].mount_idx;
    int backend_fd = vfs_open_files[fd].backend_fd;
    if (mount_idx < 0 || mount_idx >= MAX_VFS_MOUNTS || !vfs_mounts[mount_idx].used) return -2;

    VFS_Operations* ops = vfs_mounts[mount_idx].ops;
    if (!ops || !ops->read) return -3;
    return ops->read(backend_fd, buf, count);
}

int vfs_write(int fd, const char* buf, int count) {
    if (!vfs_fd_valid(fd) || !buf) return -1;

    int mount_idx = vfs_open_files[fd].mount_idx;
    int backend_fd = vfs_open_files[fd].backend_fd;
    if (mount_idx < 0 || mount_idx >= MAX_VFS_MOUNTS || !vfs_mounts[mount_idx].used) return -2;

    VFS_Operations* ops = vfs_mounts[mount_idx].ops;
    if (!ops || !ops->write) return -3;
    return ops->write(backend_fd, buf, count);
}

static void vfs_fill_dirent(DirectoryEntry* de,
                            uint32_t inode,
                            uint8_t file_type,
                            const char* name,
                            uint8_t name_len) {
    if (!de || !name) return;
    de->inode = inode;
    de->rec_len = 0;
    de->name_len = name_len;
    de->file_type = file_type;
    for (int i = 0; i < 252; i++) de->name[i] = 0;
    for (int i = 0; i < name_len && i < 252; i++) de->name[i] = name[i];
}

int vfs_readdir(const char* path, DirectoryEntry* entries, int max_entries) {
    if (!path || !entries || max_entries <= 0) return -1;

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) return -1;

    if (my_strcmp(normalized, "/proc") == 0 || my_strcmp(normalized, "/proc/") == 0) {
        int n = 0;
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 1, DIRENT_TYPE_FILE, "uptime", 6);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 2, DIRENT_TYPE_FILE, "ticks", 5);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 3, DIRENT_TYPE_FILE, "mounts", 6);
        return n;
    }

    if (my_strcmp(normalized, "/dev") == 0 || my_strcmp(normalized, "/dev/") == 0) {
        int n = 0;
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 1, DIRENT_TYPE_FILE, "null", 4);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 2, DIRENT_TYPE_FILE, "zero", 4);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 3, DIRENT_TYPE_FILE, "random", 6);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 4, DIRENT_TYPE_FILE, "stdin", 5);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 5, DIRENT_TYPE_FILE, "stdout", 6);
        if (n < max_entries) vfs_fill_dirent(&entries[n++], 6, DIRENT_TYPE_FILE, "stderr", 6);
        return n;
    }

    return newfs_readdir(normalized, entries, max_entries);
}

int vfs_mkdir(const char* path) {
    if (!path) return -1;

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) return -1;

    int mount_idx = vfs_find_mount(normalized);
    if (mount_idx < 0) return -2;

    VFS_Mount* mount = &vfs_mounts[mount_idx];
    if (!mount->ops || !mount->ops->mkdir) return -3;

    char relative_path[256];
    vfs_make_relative_path(normalized, mount->mount_point, relative_path);
    return mount->ops->mkdir(relative_path);
}

int vfs_unlink(const char* path) {
    if (!path) return -1;

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) return -1;

    int mount_idx = vfs_find_mount(normalized);
    if (mount_idx < 0) return -2;

    VFS_Mount* mount = &vfs_mounts[mount_idx];
    if (!mount->ops || !mount->ops->unlink) return -3;

    char relative_path[256];
    vfs_make_relative_path(normalized, mount->mount_point, relative_path);
    return mount->ops->unlink(relative_path);
}

int vfs_stat(const char* path, FInode* stat_out) {
    if (!path || !stat_out) return -1;

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) return -1;

    for (int i = 0; i < (int)sizeof(FInode); i++) {
        ((char*)stat_out)[i] = 0;
    }

    if (my_strcmp(normalized, "/") == 0 ||
        my_strcmp(normalized, "/proc") == 0 || my_strcmp(normalized, "/proc/") == 0 ||
        my_strcmp(normalized, "/dev") == 0 || my_strcmp(normalized, "/dev/") == 0) {
        stat_out->mode = INODE_MODE_DIR |
                         INODE_PERM_OWNER_R | INODE_PERM_OWNER_X |
                         INODE_PERM_GROUP_R | INODE_PERM_GROUP_X |
                         INODE_PERM_OTHERS_R | INODE_PERM_OTHERS_X;
        return 0;
    }

    if (my_strncmp(normalized, "/proc/", 6) == 0 || my_strncmp(normalized, "/dev/", 5) == 0) {
        stat_out->mode = INODE_MODE_FILE |
                         INODE_PERM_OWNER_R | INODE_PERM_GROUP_R | INODE_PERM_OTHERS_R;
        return 0;
    }

    return newfs_stat(normalized, stat_out);
}

void vfs_close_for_pid(int pid) {
    for (int i = 0; i < MAX_VFS_OPEN_FILES; i++) {
        if (!vfs_open_files[i].used) continue;
        if (vfs_open_files[i].owner_pid != pid) continue;
        (void)vfs_close(i);
    }
}

// ============================================================================
// Syscall Wrappers (for compatibility with old code)
// ============================================================================

// These syscall wrappers can be used directly from syscall.c
// They maintain the same interface as before

int sys_open_vfs(const char* path, int flags) {
    return vfs_open(path, flags);
}

int sys_close_vfs(int fd) {
    return vfs_close(fd);
}

int sys_read_vfs(int fd, char* buf, int count) {
    return vfs_read(fd, buf, count);
}

int sys_write_vfs(int fd, const char* buf, int count) {
    return vfs_write(fd, buf, count);
}
