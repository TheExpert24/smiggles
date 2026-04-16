// ============================================================================
// PHASE 6: Backward Compatibility Layer
// ============================================================================
// This file provides compatibility with the old filesystem API while using
// the new block-based implementation underneath.

#include "kernel.h"
#include "filesystem_new.h"
#include <stdint.h>

// External new filesystem functions
extern int fs_fd_open(const char* path, int flags);
extern int fs_fd_close(int fd);
extern int fs_fd_read(int fd, char* buffer, int count);
extern int fs_fd_write(int fd, const char* buffer, int count);
extern int fs_fd_seek(int fd, uint32_t offset, int whence);
extern int vfs_init(void);
extern int vfs_open(const char* path, int flags);
extern int vfs_close(int fd);
extern int vfs_read(int fd, char* buf, int count);
extern int vfs_write(int fd, const char* buf, int count);

// ============================================================================
// Legacy FSNode Compatibility (if still needed)
// ============================================================================

// Global variables for legacy compatibility
FSNode node_table[MAX_NODES];
int node_count = 0;
int current_dir_idx = 0;
RamDir dir_table[MAX_DIRS] = { {"root", 1, -1} };
int dir_count = 1;
int current_dir = 0;

// ============================================================================
// Initialization
// ============================================================================

int fs_init(void) {
    // Initialize new block-based filesystem
    if (vfs_init() != 0) {
        return -1;
    }
    
    return 0;
}

// ============================================================================
// File Descriptor Operations (Compatibility Wrappers)
// ============================================================================

// These maintain the same API as the old filesystem.c but use the new
// block-based implementation underneath.

// open() wrapper
int fs_open(const char* path, int flags) {
    if (!path) return -1;
    return fs_fd_open(path, flags);
}

// close() wrapper
int fs_close(int fd) {
    if (fd < 0) return -1;
    return fs_fd_close(fd);
}

// read() wrapper
int fs_read(int fd, char* buf, int count) {
    if (fd < 0 || !buf) return -1;
    return fs_fd_read(fd, buf, count);
}

// write() wrapper
int fs_write(int fd, const char* buf, int count) {
    if (fd < 0 || !buf) return -1;
    return fs_fd_write(fd, buf, count);
}

// seek() wrapper
int fs_seek(int fd, int offset, int whence) {
    if (fd < 0) return -1;
    return fs_fd_seek(fd, (uint32_t)offset, whence);
}

// ============================================================================
// Legacy Syscall Handlers (updated to use new FS)
// ============================================================================

// These syscall handlers can be used directly from syscall.c
// They call through to the new block-based filesystem

int sys_open_new(const char* path, int flags) {
    return fs_fd_open(path, flags);
}

int sys_close_new(int fd) {
    return fs_fd_close(fd);
}

int sys_read_new(int fd, char* buf, int count) {
    return fs_fd_read(fd, buf, count);
}

int sys_write_new(int fd, const char* buf, int count) {
    return fs_fd_write(fd, buf, count);
}

// ============================================================================
// Save/Restore for Legacy Code Compatibility
// ============================================================================

// fs_save() - modern implementation flushes all dirty data
void fs_save(void) {
    // The new filesystem auto-flushes on every write via disk_fs_flush()
    // This is a no-op for compatibility
    return;
}

// fs_load() - modern implementation loads on-demand
void fs_load(void) {
    // The new filesystem loads blocks on-demand
    // This is a no-op for compatibility
    return;
}

// ============================================================================
// Directory Operations for Legacy Code
// ============================================================================

// These functions provide backward compatibility for code using the old
// in-memory node table approach

int resolve_path(const char* path) {
    // Legacy function that returned node_idx
    // Now just verify the path exists
    FInode inode;
    return path_resolve(path, &inode);
}

int fs_touch(const char* path, const char* initial_content) {
    // Create a file with optional initial content
    int inode_num = fs_fd_open(path, FS_O_CREATE | FS_O_WRITE);
    
    if (inode_num < 0) {
        return -1;
    }
    
    if (initial_content) {
        int len = 0;
        while (initial_content[len]) len++;
        fs_fd_write(inode_num, initial_content, len);
    }
    
    fs_fd_close(inode_num);
    
    // Return a dummy node index for backward compatibility
    return 1;
}

// ============================================================================
// Legacy Node Table Access (DEPRECATED)
// ============================================================================

// These are stubs for backward compatibility only
// New code should use the block-based filesystem directly

int fs_find_node(const char* name) {
    // Deprecated - always return -1 (not found in legacy table)
    return -1;
}

int fs_add_node(const char* name, int type) {
    // Deprecated - use fs_touch or inode_create_* instead
    return -1;
}

int fs_remove_node(int node_idx) {
    // Deprecated - use fs_unlink instead
    return -1;
}

void fs_close_all_by_pid(int pid) {
    // Deprecated - file descriptors are process-scoped now
    // This would need proper implementation in a real multitasking kernel
}

// ============================================================================
// Utility Functions
// ============================================================================

// Print filesystem info (for debugging)
void fs_print_info(char* video, int* cursor, unsigned char color) {
    extern void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
    
    char buffer[256];
    const char* msg = "Block-based filesystem (ext2-like)\n";
    print_string(msg, 35, video, cursor, color);
    
    msg = "- 4096 inodes, 2048 blocks (8MB data)\n";
    print_string(msg, 39, video, cursor, color);
    
    msg = "- VFS support: /disk, /proc, /dev, /tmp\n";
    print_string(msg, 41, video, cursor, color);
}

// ============================================================================
// Migration Helpers (optional)
// ============================================================================

// For systems that want to migrate from old ramfs to new disk FS
// These can be called during boot to set up initial filesystem

int fs_create_root_directory(void) {
    // Ensure root directory exists
    // This should be created during filesystem format
    return 0;
}

int fs_migrate_from_legacy(void) {
    // If the old in-memory node_table had data, migrate it
    // This is optional and would be called once during boot
    // Implementation would copy data from node_table to disk FS
    
    for (int i = 0; i < node_count; i++) {
        if (!node_table[i].used) continue;
        
        // TODO: Create corresponding inode in new FS
        // and populate with content from node_table[i].content
    }
    
    return 0;
}

// ============================================================================
// Error Messages
// ============================================================================

const char* fs_strerror(int error_code) {
    switch (error_code) {
        case -1: return "Invalid argument";
        case -2: return "Invalid file descriptor";
        case -3: return "File not found";
        case -4: return "Create failed";
        case -5: return "Read error";
        case -6: return "Write error";
        case -7: return "Too many open files";
        case -8: return "Permission denied";
        case -9: return "No space left on device";
        case -10: return "I/O error";
        default: return "Unknown error";
    }
}
