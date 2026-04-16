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

// ============================================================================
// VFS Mount Table
// ============================================================================

#define MAX_VFS_MOUNTS 16
static VFS_Mount vfs_mounts[MAX_VFS_MOUNTS];
static int vfs_mount_count = 0;

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

typedef struct {
    int fd;  // Kernel file descriptor
} DiskFile;

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

static VFS_Operations vfs_disk_ops = {
    .open = vfs_disk_open,
    .close = vfs_disk_close,
    .read = vfs_disk_read,
    .write = vfs_disk_write,
    .readdir = 0,  // TODO: implement directory listing
    .mkdir = 0,
    .unlink = 0
};

// ============================================================================
// /proc Virtual Filesystem Backend
// ============================================================================

typedef struct {
    char name[32];
    const char* (*get_content)(void);  // Function to get file content
} ProcFile;

static const char* proc_uptime_content(void) {
    // Placeholder - would return uptime in kernel_time.c
    extern volatile int ticks;
    static char buffer[128];
    int uptime_ms = ticks * 10;  // Assuming 10ms per tick
    UNUSED(uptime_ms);
    buffer[0] = 0;
    return buffer;
}

static ProcFile proc_files[] __attribute__((unused)) = {
    { "uptime", proc_uptime_content },
};

static int vfs_proc_open(const char* path, int flags) {
    UNUSED(flags);
    if (!path) return -1;
    
    // Simple /proc implementation - read-only
    if (my_strcmp(path, "/") == 0) {
        return -2;  // Can't open directory
    }
    
    // For now, return a dummy FD
    // In a real system, we'd return a special FD that reads from proc_files
    return -1;  // Not implemented yet
}

static int vfs_proc_close(int fd) {
    UNUSED(fd);
    return 0;  // Dummy implementation
}

static int vfs_proc_read(int fd, char* buf, int count) {
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);
    return 0;  // Dummy implementation - return 0 bytes
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
    
    // Placeholder - would maintain open device table
    return -1;  // Not implemented yet
}

static int vfs_dev_close(int fd) {
    UNUSED(fd);
    return 0;
}

static int vfs_dev_read(int fd, char* buf, int count) {
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);
    return 0;  // Placeholder
}

static int vfs_dev_write(int fd, const char* buf, int count) {
    UNUSED(fd);
    UNUSED(buf);
    return count;  // Dummy - pretend to write
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
static int vfs_tmp_open(const char* path, int flags) {
    UNUSED(path);
    UNUSED(flags);
    // Placeholder - could use a separate in-memory filesystem
    return -1;  // Not implemented yet
}

static int vfs_tmp_close(int fd) {
    UNUSED(fd);
    return 0;
}

static int vfs_tmp_read(int fd, char* buf, int count) {
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);
    return 0;
}

static int vfs_tmp_write(int fd, const char* buf, int count) {
    UNUSED(fd);
    UNUSED(buf);
    return count;
}

static VFS_Operations vfs_tmp_ops = {
    .open = vfs_tmp_open,
    .close = vfs_tmp_close,
    .read = vfs_tmp_read,
    .write = vfs_tmp_write,
    .readdir = 0,
    .mkdir = 0,
    .unlink = 0
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
    
    // Find the mount for this path
    int mount_idx = vfs_find_mount(path);
    if (mount_idx < 0) {
        return -2;  // No mount found
    }
    
    VFS_Mount* mount = &vfs_mounts[mount_idx];
    if (!mount->ops || !mount->ops->open) {
        return -3;  // Mount doesn't support open
    }
    
    // Convert path to relative path for the mount
    char relative_path[256];
    vfs_make_relative_path(path, mount->mount_point, relative_path);
    
    // Call the mount's open function
    return mount->ops->open(relative_path, flags);
}

int vfs_close(int fd) {
    if (fd < 0) return -1;
    
    // For now, assume all FDs use disk ops
    // In a real implementation, we'd need to track which mount owns each FD
    return vfs_disk_close(fd);
}

int vfs_read(int fd, char* buf, int count) {
    if (fd < 0 || !buf) return -1;
    
    // For now, assume all FDs use disk ops
    return vfs_disk_read(fd, buf, count);
}

int vfs_write(int fd, const char* buf, int count) {
    if (fd < 0 || !buf) return -1;
    
    // For now, assume all FDs use disk ops
    return vfs_disk_write(fd, buf, count);
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
