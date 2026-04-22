// ============================================================================
// PHASE 4: Block-Based File I/O
// ============================================================================

#include "kernel.h"
#include "filesystem_new.h"
#include <stdint.h>

// Export legacy fs_fd_* symbols and provide newfs_* wrappers so the
// compatibility layer and the new shell/syscall path can share the same code.
#define fs_stat      newfs_stat
#define fs_mkdir     newfs_mkdir
#define fs_rmdir     newfs_rmdir
#define fs_unlink    newfs_unlink
#define fs_readdir   newfs_readdir
#define fs_touch     newfs_touch

// External functions
extern int disk_read_block(uint32_t block, uint8_t* buf);
extern int disk_write_block(uint32_t block, const uint8_t* buf);
extern int disk_read_inode(uint32_t inode_num, FInode* out);
extern int disk_write_inode(uint32_t inode_num, const FInode* inode);
extern uint32_t inode_allocate(void);
extern void inode_free(uint32_t inode_num);
extern int disk_allocate_block(void);
extern void disk_free_block(uint32_t block_num);
extern uint32_t inode_get_block(FInode* inode, uint32_t file_block_idx);
extern int inode_set_block(FInode* inode, uint32_t file_block_idx, uint32_t block_num);
extern int path_resolve(const char* path, FInode* inode_out);
extern int path_resolve_parent(const char* path, char* name_out, FInode* parent_out);
extern int inode_create_file(const char* path, uint16_t mode, uint16_t uid, uint16_t gid);
extern int inode_create_directory(const char* path, uint16_t mode, uint16_t uid, uint16_t gid);
extern int inode_delete_file(const char* path);
extern int inode_add_entry(uint32_t dir_inode_num, const char* name, uint32_t inode_num, uint8_t file_type);
extern int inode_remove_entry(uint32_t dir_inode_num, const char* name);
extern int inode_find_entry(uint32_t dir_inode_num, const char* name);
extern int inode_read_directory(uint32_t inode_num, DirectoryEntry* entries, int max_entries);

extern int disk_fs_flush(void);

// From kernel.h - needed for process context
extern int current_process;

// Utility functions
static void* my_memcpy(void* dest, const void* src, int n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

static void my_memset(void* dest, int value, int n) {
    unsigned char* d = (unsigned char*)dest;
    for (int i = 0; i < n; i++) d[i] = (unsigned char)value;
}

static void rollback_file_write(const FInode* original_inode,
                                const unsigned char* original_indirect_buf,
                                const FInode* updated_inode) {
    unsigned char updated_indirect_buf[FS_BLOCK_SIZE];

    if (!original_inode || !updated_inode) return;

    for (int i = 0; i < 12; i++) {
        if (original_inode->direct_blocks[i] == 0 && updated_inode->direct_blocks[i] != 0) {
            disk_free_block(updated_inode->direct_blocks[i]);
        }
    }

    if (updated_inode->indirect_block == 0) {
        return;
    }

    if (disk_read_block(updated_inode->indirect_block, updated_indirect_buf) != 0) {
        if (original_inode->indirect_block == 0) {
            disk_free_block(updated_inode->indirect_block);
        }
        return;
    }

    if (original_inode->indirect_block == 0) {
        uint32_t* updated_entries = (uint32_t*)updated_indirect_buf;
        for (int i = 0; i < 1024; i++) {
            if (updated_entries[i] != 0) {
                disk_free_block(updated_entries[i]);
            }
        }
        disk_free_block(updated_inode->indirect_block);
        return;
    }

    if (original_inode->indirect_block == updated_inode->indirect_block && original_indirect_buf) {
        const uint32_t* original_entries = (const uint32_t*)original_indirect_buf;
        const uint32_t* updated_entries = (const uint32_t*)updated_indirect_buf;
        for (int i = 0; i < 1024; i++) {
            if (original_entries[i] == 0 && updated_entries[i] != 0) {
                disk_free_block(updated_entries[i]);
            }
        }
    }
}

// ============================================================================
// File Descriptor Table
// ============================================================================

#define MAX_OPEN_FDS 256
static KernelFD_NewFS fd_table[MAX_OPEN_FDS];

void fs_fd_init(void) {
    for (int i = 0; i < MAX_OPEN_FDS; i++) {
        fd_table[i].used = 0;
        fd_table[i].inode_num = 0;
        fd_table[i].offset = 0;
        fd_table[i].flags = 0;
        fd_table[i].owner_pid = -1;
        fd_table[i].is_vfs_mount = 0;
        fd_table[i].vfs_ops = 0;
        fd_table[i].vfs_private = 0;
    }
}

static int fs_fd_is_valid(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FDS) return 0;
    if (!fd_table[fd].used) return 0;
    if (fd_table[fd].inode_num < 0) return 0;
    return 1;
}

// ============================================================================
// Open File - Main Entry Point
// ============================================================================

int fs_fd_open(const char* path, int flags) {
    if (!path || path[0] == 0) return -1;
    if ((flags & (FS_O_READ | FS_O_WRITE)) == 0) return -2;
    
    // Try to resolve path
    FInode inode;
    int inode_num = path_resolve(path, &inode);
    
    if (inode_num < 0) {
        // File doesn't exist
        if (!(flags & FS_O_CREATE)) {
            return -3;  // File not found and create not requested
        }
        
        // Create new file
        inode_num = inode_create_file(path, INODE_PERM_OWNER_R | INODE_PERM_OWNER_W, 0, 0);
        if (inode_num < 0) {
            return -4;  // Creation failed
        }
        
        // Read the newly created inode
        if (disk_read_inode(inode_num, &inode) != 0) {
            return -5;
        }
    }
    
    // Truncate if requested
    if ((flags & FS_O_TRUNC) && (flags & FS_O_WRITE)) {
        // Free all existing blocks
        uint32_t file_block_idx = 0;
        while (1) {
            uint32_t block_num = inode_get_block(&inode, file_block_idx);
            if (block_num == 0 || block_num == (uint32_t)-1) {
                break;
            }
            disk_free_block(block_num);
            file_block_idx++;
        }
        
        if (inode.indirect_block != 0) {
            disk_free_block(inode.indirect_block);
            inode.indirect_block = 0;
        }
        
        // Clear all direct blocks
        for (int i = 0; i < 12; i++) {
            inode.direct_blocks[i] = 0;
        }
        
        inode.size = 0;
        
        if (disk_write_inode(inode_num, &inode) != 0) {
            return -6;
        }
    }
    
    // Find free file descriptor
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FDS; i++) {
        if (!fd_table[i].used) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        return -7;  // Too many open files
    }
    
    // Initialize file descriptor
    fd_table[fd].used = 1;
    fd_table[fd].inode_num = inode_num;
    fd_table[fd].flags = flags;
    fd_table[fd].owner_pid = current_process;
    fd_table[fd].is_vfs_mount = 0;
    fd_table[fd].vfs_ops = 0;
    fd_table[fd].vfs_private = 0;
    
    if (flags & FS_O_APPEND) {
        fd_table[fd].offset = inode.size;
    } else {
        fd_table[fd].offset = 0;
    }
    
    return fd;
}

// ============================================================================
// Close File
// ============================================================================

int fs_fd_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FDS) return -1;
    if (!fd_table[fd].used) return -2;
    
    // Flush any pending data
    disk_fs_flush();
    
    fd_table[fd].used = 0;
    fd_table[fd].inode_num = 0;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = 0;
    fd_table[fd].owner_pid = -1;
    fd_table[fd].is_vfs_mount = 0;
    fd_table[fd].vfs_ops = 0;
    fd_table[fd].vfs_private = 0;
    
    return 0;
}

void fs_fd_close_for_pid(int pid) {
    for (int i = 0; i < MAX_OPEN_FDS; i++) {
        if (!fd_table[i].used) continue;
        if (fd_table[i].owner_pid == pid) {
            fs_fd_close(i);
        }
    }
}

// ============================================================================
// Read File
// ============================================================================

int fs_fd_read(int fd, char* buffer, int count) {
    if (!buffer || count < 0) return -1;
    if (!fs_fd_is_valid(fd)) return -2;
    if (!(fd_table[fd].flags & FS_O_READ)) return -3;
    
    FInode inode;
    if (disk_read_inode(fd_table[fd].inode_num, &inode) != 0) {
        return -4;
    }
    
    uint32_t offset = fd_table[fd].offset;
    if (offset >= inode.size) {
        return 0;  // End of file
    }

    int available = (int)(inode.size - offset);
    
    int to_read = count;
    if (to_read > available) {
        to_read = available;
    }
    
    unsigned char block_buf[4096];
    int bytes_read = 0;
    
    while (bytes_read < to_read) {
        uint32_t file_block_idx = offset / 4096;
        uint32_t block_offset = offset % 4096;
        
        uint32_t block_num = inode_get_block(&inode, file_block_idx);
        if (block_num == 0 || block_num == (uint32_t)-1) {
            break;  // Sparse file
        }
        
        if (disk_read_block(block_num, block_buf) != 0) {
            return -5;  // Read error
        }
        
        int to_copy = (int)(4096u - block_offset);
        if (to_copy > (to_read - bytes_read)) {
            to_copy = to_read - bytes_read;
        }
        
        my_memcpy(buffer + bytes_read, block_buf + block_offset, to_copy);
        
        bytes_read += to_copy;
        offset += to_copy;
    }
    
    fd_table[fd].offset = offset;
    return bytes_read;
}

// ============================================================================
// Write File
// ============================================================================

int fs_fd_write(int fd, const char* buffer, int count) {
    if (!buffer || count < 0) return -1;
    if (!fs_fd_is_valid(fd)) return -2;
    if (!(fd_table[fd].flags & FS_O_WRITE)) return -3;
    
    FInode inode;
    if (disk_read_inode(fd_table[fd].inode_num, &inode) != 0) {
        return -4;
    }

    FInode original_inode = inode;
    unsigned char original_indirect_buf[FS_BLOCK_SIZE];
    unsigned char has_original_indirect = 0;

    if (original_inode.indirect_block != 0) {
        if (disk_read_block(original_inode.indirect_block, original_indirect_buf) == 0) {
            has_original_indirect = 1;
        }
    }
    
    uint32_t offset = fd_table[fd].offset;
    int bytes_written = 0;
    
    unsigned char block_buf[4096];
    
    while (bytes_written < count) {
        uint32_t file_block_idx = offset / 4096;
        uint32_t block_offset = offset % 4096;
        
        // Get or allocate block
        uint32_t block_num = inode_get_block(&inode, file_block_idx);
        
        if (block_num == 0 || block_num == (uint32_t)-1) {
            // Need to allocate new block
            int allocated_block = disk_allocate_block();
            if (allocated_block < 0) {
                rollback_file_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
                return -5;  // Allocation failed
            }
            block_num = (uint32_t)allocated_block;
            
            // Initialize block
            my_memset(block_buf, 0, 4096);
            if (disk_write_block(block_num, block_buf) != 0) {
                disk_free_block(block_num);
                rollback_file_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
                return -6;
            }
            
            // Link to inode
            if (inode_set_block(&inode, file_block_idx, block_num) != 0) {
                disk_free_block(block_num);
                rollback_file_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
                return -7;
            }
        } else {
            // Read existing block (may be partially filled)
            if (disk_read_block(block_num, block_buf) != 0) {
                rollback_file_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
                return -8;
            }
        }
        
        int to_copy = (int)(4096u - block_offset);
        if (to_copy > (count - bytes_written)) {
            to_copy = count - bytes_written;
        }
        
        my_memcpy(block_buf + block_offset, buffer + bytes_written, to_copy);
        
        if (disk_write_block(block_num, block_buf) != 0) {
            rollback_file_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
            return -9;  // Write failed
        }
        
        bytes_written += to_copy;
        offset += to_copy;
    }
    
    fd_table[fd].offset = offset;
    
    // Update file size if we extended it
    if (offset > inode.size) {
        inode.size = offset;
    }
    
    // Write inode back
    if (disk_write_inode(fd_table[fd].inode_num, &inode) != 0) {
        rollback_file_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
        return -10;
    }
    
    // Flush changes
    disk_fs_flush();
    
    return bytes_written;
}

// ============================================================================
// Seek Operations
// ============================================================================

int fs_fd_seek(int fd, uint32_t offset, int whence) {
    if (!fs_fd_is_valid(fd)) return -1;
    
    FInode inode;
    if (disk_read_inode(fd_table[fd].inode_num, &inode) != 0) {
        return -2;
    }
    
    uint32_t new_offset = 0;
    
    switch (whence) {
        case 0:  // SEEK_SET (absolute)
            new_offset = offset;
            break;
        case 1:  // SEEK_CUR (relative to current)
            new_offset = fd_table[fd].offset + offset;
            break;
        case 2:  // SEEK_END (relative to end)
            new_offset = inode.size + offset;
            break;
        default:
            return -3;  // Invalid whence
    }
    
    fd_table[fd].offset = new_offset;
    return new_offset;
}

// ============================================================================
// File Metadata Operations
// ============================================================================

int fs_stat(const char* path, FInode* stat_out) {
    if (!path || !stat_out) return -1;
    
    int inode_num = path_resolve(path, stat_out);
    return inode_num;
}

int fs_mkdir(const char* path) {
    if (!path) return -1;
    
    int inode_num = inode_create_directory(path, 
                                          INODE_PERM_OWNER_R | INODE_PERM_OWNER_W | INODE_PERM_OWNER_X,
                                          0, 0);
    return inode_num;
}

int fs_rmdir(const char* path) {
    if (!path) return -1;
    
    return inode_delete_file(path);
}

int fs_unlink(const char* path) {
    if (!path) return -1;
    
    return inode_delete_file(path);
}

// ============================================================================
// Directory Listing
// ============================================================================

int fs_readdir(const char* path, DirectoryEntry* entries, int max_entries) {
    if (!path || !entries) return -1;
    
    FInode inode;
    int inode_num = path_resolve(path, &inode);
    
    if (inode_num < 0) {
        return -2;  // Path not found
    }
    
    if (!(inode.mode & INODE_MODE_DIR)) {
        return -3;  // Not a directory
    }
    
    return inode_read_directory(inode_num, entries, max_entries);
}

// ============================================================================
// New-FS Wrappers
// ============================================================================

void newfs_fd_init(void) { fs_fd_init(); }
int newfs_fd_open(const char* path, int flags) { return fs_fd_open(path, flags); }
int newfs_fd_close(int fd) { return fs_fd_close(fd); }
int newfs_fd_read(int fd, char* buffer, int count) { return fs_fd_read(fd, buffer, count); }
int newfs_fd_write(int fd, const char* buffer, int count) { return fs_fd_write(fd, buffer, count); }
int newfs_fd_seek(int fd, uint32_t offset, int whence) { return fs_fd_seek(fd, offset, whence); }
