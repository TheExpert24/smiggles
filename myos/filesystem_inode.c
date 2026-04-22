// ============================================================================
// PHASE 3: Inode and Directory Operations
// ============================================================================

#include "kernel.h"
#include "filesystem_new.h"
#include <stdint.h>

// External functions from filesystem_disk.c
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

static void rollback_directory_write(const FInode* original_inode,
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
// Directory Operations
// ============================================================================

// Read directory entries from an inode
// Returns number of entries read, -1 on error
int inode_read_directory(uint32_t inode_num, DirectoryEntry* entries, int max_entries) {
    FInode inode;
    if (disk_read_inode(inode_num, &inode) != 0) {
        return -1;
    }
    
    if (!(inode.mode & INODE_MODE_DIR)) {
        return -1;  // Not a directory
    }
    
    unsigned char block_buf[4096];
    int entry_count = 0;
    uint32_t bytes_read = 0;
    uint32_t file_block_idx = 0;
    
    while (bytes_read < inode.size && entry_count < max_entries) {
        uint32_t block_num = inode_get_block(&inode, file_block_idx);
        if (block_num == 0 || block_num == (uint32_t)-1) {
            break;  // No more blocks
        }
        
        if (disk_read_block(block_num, block_buf) != 0) {
            return -1;  // Read error
        }
        
        // Parse directory entries from block
        int block_offset = 0;
        while (block_offset < 4096 && entry_count < max_entries && bytes_read < inode.size) {
            DirectoryEntry* entry = (DirectoryEntry*)(block_buf + block_offset);
            
            if (entry->rec_len == 0) break;  // End of entries in this block
            
            // Copy entry
            my_memcpy(&entries[entry_count], entry, sizeof(DirectoryEntry));
            entry_count++;
            
            block_offset += entry->rec_len;
            bytes_read += entry->rec_len;
        }
        
        file_block_idx++;
    }
    
    return entry_count;
}

// Write directory entries to an inode
int inode_write_directory(uint32_t inode_num, const DirectoryEntry* entries, int entry_count) {
    FInode inode;
    if (disk_read_inode(inode_num, &inode) != 0) {
        return -1;
    }

    FInode original_inode = inode;
    unsigned char original_indirect_buf[FS_BLOCK_SIZE];
    unsigned char has_original_indirect = 0;

    if (original_inode.indirect_block != 0) {
        if (disk_read_block(original_inode.indirect_block, original_indirect_buf) == 0) {
            has_original_indirect = 1;
        }
    }
    
    if (!(inode.mode & INODE_MODE_DIR)) {
        return -1;  // Not a directory
    }
    
    unsigned char block_buf[4096];
    int bytes_written = 0;
    int entry_idx = 0;
    uint32_t file_block_idx = 0;
    
    while (entry_idx < entry_count) {
        my_memset(block_buf, 0, 4096);
        
        int block_offset = 0;
        while (entry_idx < entry_count && block_offset + 260 <= 4096) {
            DirectoryEntry* block_entry = (DirectoryEntry*)(block_buf + block_offset);
            
            my_memcpy(block_entry, &entries[entry_idx], sizeof(DirectoryEntry));
            block_entry->rec_len = 260;  // Fixed record length
            
            block_offset += 260;
            entry_idx++;
            bytes_written += 260;
        }
        
        // Allocate or get block
        int block_num = (int)inode_get_block(&inode, file_block_idx);
        if (block_num == 0 || block_num == -1) {
            block_num = disk_allocate_block();
            if (block_num < 0) {
                rollback_directory_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
                return -1;
            }
            
            if (inode_set_block(&inode, file_block_idx, (uint32_t)block_num) != 0) {
                disk_free_block((uint32_t)block_num);
                rollback_directory_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
                return -1;
            }
        }
        
        if (disk_write_block((uint32_t)block_num, block_buf) != 0) {
            rollback_directory_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
            return -1;
        }
        
        file_block_idx++;
    }
    
    inode.size = bytes_written;
    if (disk_write_inode(inode_num, &inode) != 0) {
        rollback_directory_write(&original_inode, has_original_indirect ? original_indirect_buf : 0, &inode);
        return -1;
    }
    
    return 0;
}

// Find an entry in a directory by name
// Returns inode number or -1 if not found
int inode_find_entry(uint32_t dir_inode_num, const char* name) {
    DirectoryEntry entries[128];
    int entry_count = inode_read_directory(dir_inode_num, entries, 128);
    
    if (entry_count < 0) {
        return -1;
    }
    
    for (int i = 0; i < entry_count; i++) {
        if (my_strcmp(entries[i].name, name) == 0) {
            return entries[i].inode;
        }
    }
    
    return -1;  // Not found
}

// Add an entry to a directory
int inode_add_entry(uint32_t dir_inode_num, const char* name, uint32_t inode_num, uint8_t file_type) {
    DirectoryEntry entries[128];
    int entry_count = inode_read_directory(dir_inode_num, entries, 128);
    
    if (entry_count < 0) {
        return -1;
    }
    
    if (entry_count >= 128) {
        return -1;  // Directory full
    }
    
    // Add new entry
    DirectoryEntry* new_entry = &entries[entry_count];
    int name_len = my_strlen(name);

    if (!name || name_len <= 0 || name_len > DIRENT_NAME_MAX) {
        return -1;
    }

    my_memset(new_entry, 0, sizeof(DirectoryEntry));
    new_entry->inode = inode_num;
    new_entry->file_type = file_type;
    new_entry->name_len = (uint8_t)name_len;
    my_memcpy(new_entry->name, name, name_len);
    
    // Write back
    return inode_write_directory(dir_inode_num, entries, entry_count + 1);
}

// Remove an entry from a directory
int inode_remove_entry(uint32_t dir_inode_num, const char* name) {
    DirectoryEntry entries[128];
    int entry_count = inode_read_directory(dir_inode_num, entries, 128);
    
    if (entry_count < 0) {
        return -1;
    }
    
    int remove_idx = -1;
    for (int i = 0; i < entry_count; i++) {
        if (my_strcmp(entries[i].name, name) == 0) {
            remove_idx = i;
            break;
        }
    }
    
    if (remove_idx < 0) {
        return -1;  // Not found
    }
    
    // Remove by shifting
    for (int i = remove_idx; i < entry_count - 1; i++) {
        my_memcpy(&entries[i], &entries[i + 1], sizeof(DirectoryEntry));
    }
    
    // Write back
    return inode_write_directory(dir_inode_num, entries, entry_count - 1);
}

// ============================================================================
// Path Resolution
// ============================================================================

// Split path into components
// Returns number of components, -1 on error
static int path_split(const char* path, char** components, int max_components) {
    if (!path || path[0] != '/') {
        return -1;  // Must be absolute path
    }
    
    int comp_count = 0;
    const char* start = path + 1;  // Skip leading /
    
    while (*start && comp_count < max_components) {
        // Find end of component
        const char* end = start;
        while (*end && *end != '/') {
            end++;
        }
        
        if (end > start) {
            components[comp_count] = (char*)start;
            comp_count++;
        }
        
        if (!*end) break;
        start = end + 1;
    }
    
    return comp_count;
}

// Resolve a path to an inode
// Returns inode number or -1 if not found
int path_resolve(const char* path, FInode* inode_out) {
    if (!path || !inode_out) {
        return -1;
    }

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) {
        return -1;
    }
    
    // Start from root (inode 1 typically)
    uint32_t current_inode = 1;
    
    // Special case: root path
    if (my_strcmp(normalized, "/") == 0) {
        if (disk_read_inode(current_inode, inode_out) != 0) {
            return -1;
        }
        return current_inode;
    }
    
    // Split path into components
    char* components[32];
    int comp_count = path_split(normalized, components, 32);
    
    if (comp_count < 0) {
        return -1;
    }
    
    // Traverse path
    for (int i = 0; i < comp_count; i++) {
        // Extract component name (null-terminate it for comparison)
        char* comp = components[i];
        char* comp_end = comp;
        while (*comp_end && *comp_end != '/') {
            comp_end++;
        }
        int comp_len = comp_end - comp;
        
        if (comp_len == 0) continue;  // Skip empty components
        
        // Create temporary null-terminated name
        char name[256];
        if (comp_len >= 256) {
            return -1;  // Name too long
        }
        my_memcpy(name, comp, comp_len);
        name[comp_len] = 0;
        
        // Find entry in current directory
        int next_inode = inode_find_entry(current_inode, name);
        if (next_inode < 0) {
            return -1;  // Component not found
        }
        
        current_inode = next_inode;
    }
    
    if (disk_read_inode(current_inode, inode_out) != 0) {
        return -1;
    }
    
    return current_inode;
}

// Resolve a path to its parent directory and filename
// Returns parent inode number, -1 on error
int path_resolve_parent(const char* path, char* name_out, FInode* parent_out) {
    if (!path || !name_out || !parent_out) {
        return -1;
    }

    char normalized[256];
    if (!fs_path_normalize(path, normalized, (int)sizeof(normalized))) {
        return -1;
    }
    
    // Find last slash
    const char* last_slash = normalized;
    const char* p = normalized;
    while (*p) {
        if (*p == '/') {
            last_slash = p;
        }
        p++;
    }
    
    // Extract filename
    const char* filename = last_slash;
    if (*filename == '/') filename++;
    int name_len = my_strlen(filename);
    
    if (name_len == 0 || name_len > DIRENT_NAME_MAX) {
        return -1;  // Invalid filename
    }
    
    my_memcpy(name_out, filename, name_len);
    name_out[name_len] = 0;
    
    // Resolve parent path
    char parent_path[256];
    if (last_slash == normalized) {
        // Parent is root
        parent_path[0] = '/';
        parent_path[1] = 0;
    } else {
        int parent_len = last_slash - normalized;
        if (parent_len >= 256) {
            return -1;
        }
        my_memcpy(parent_path, normalized, parent_len);
        parent_path[parent_len] = 0;
    }
    
    return path_resolve(parent_path, parent_out);
}

// ============================================================================
// File Creation and Deletion
// ============================================================================

// Create a new file
// Returns inode number or -1 on error
int inode_create_file(const char* path, uint16_t mode, uint16_t uid, uint16_t gid) {
    // Allocate new inode
    uint32_t inode_num = inode_allocate();
    if (inode_num == (uint32_t)-1) {
        return -1;  // No free inodes
    }
    
    // Get parent directory and filename
    char filename[256];
    FInode parent_inode;
    int parent_inode_num = path_resolve_parent(path, filename, &parent_inode);
    
    if (parent_inode_num < 0) {
        inode_free(inode_num);
        return -1;  // Parent not found
    }
    
    // Initialize new inode
    FInode new_inode;
    my_memset(&new_inode, 0, sizeof(FInode));
    new_inode.mode = INODE_MODE_FILE | mode;
    new_inode.uid = uid;
    new_inode.gid = gid;
    new_inode.link_count = 1;
    new_inode.size = 0;
    
    if (disk_write_inode(inode_num, &new_inode) != 0) {
        inode_free(inode_num);
        return -1;
    }
    
    // Add entry to parent directory
    if (inode_add_entry(parent_inode_num, filename, inode_num, DIRENT_TYPE_FILE) != 0) {
        inode_free(inode_num);
        return -1;
    }
    
    return inode_num;
}

// Create a new directory
// Returns inode number or -1 on error
int inode_create_directory(const char* path, uint16_t mode, uint16_t uid, uint16_t gid) {
    // Allocate new inode
    uint32_t inode_num = inode_allocate();
    if (inode_num == (uint32_t)-1) {
        return -1;  // No free inodes
    }
    
    // Get parent directory and filename
    char filename[256];
    FInode parent_inode;
    int parent_inode_num = path_resolve_parent(path, filename, &parent_inode);
    
    if (parent_inode_num < 0) {
        inode_free(inode_num);
        return -1;  // Parent not found
    }
    
    // Initialize new inode as directory
    FInode new_inode;
    my_memset(&new_inode, 0, sizeof(FInode));
    new_inode.mode = INODE_MODE_DIR | mode;
    new_inode.uid = uid;
    new_inode.gid = gid;
    new_inode.link_count = 2;  // . and ..
    new_inode.size = 0;
    
    if (disk_write_inode(inode_num, &new_inode) != 0) {
        inode_free(inode_num);
        return -1;
    }
    
    // Create . and .. entries
    DirectoryEntry entries[2];
    
    my_memset(&entries[0], 0, sizeof(DirectoryEntry));
    entries[0].inode = inode_num;
    entries[0].file_type = DIRENT_TYPE_DIR;
    entries[0].name_len = 1;
    entries[0].name[0] = '.';
    
    my_memset(&entries[1], 0, sizeof(DirectoryEntry));
    entries[1].inode = parent_inode_num;
    entries[1].file_type = DIRENT_TYPE_DIR;
    entries[1].name_len = 2;
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    
    if (inode_write_directory(inode_num, entries, 2) != 0) {
        inode_free(inode_num);
        return -1;
    }
    
    // Add entry to parent directory
    if (inode_add_entry(parent_inode_num, filename, inode_num, DIRENT_TYPE_DIR) != 0) {
        inode_free(inode_num);
        return -1;
    }
    
    return inode_num;
}

// Delete a file (unlink)
int inode_delete_file(const char* path) {
    FInode inode;
    int inode_num = path_resolve(path, &inode);
    
    if (inode_num < 0) {
        return -1;  // File not found
    }
    
    // Get parent and filename for removal
    char filename[256];
    FInode parent_inode;
    int parent_inode_num = path_resolve_parent(path, filename, &parent_inode);
    
    if (parent_inode_num < 0) {
        return -1;
    }
    
    // Free all blocks used by file
    uint32_t file_block_idx = 0;
    while (1) {
        uint32_t block_num = inode_get_block(&inode, file_block_idx);
        if (block_num == 0 || block_num == (uint32_t)-1) {
            break;
        }
        disk_free_block(block_num);
        file_block_idx++;
    }
    
    // Free indirect block if it exists
    if (inode.indirect_block != 0) {
        disk_free_block(inode.indirect_block);
    }
    
    // Remove from parent directory
    if (inode_remove_entry(parent_inode_num, filename) != 0) {
        return -1;
    }
    
    // Free inode
    inode_free(inode_num);
    
    return 0;
}
