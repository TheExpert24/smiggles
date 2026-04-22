#ifndef FILESYSTEM_NEW_H
#define FILESYSTEM_NEW_H

#include <stdint.h>

// ============================================================================
// On-Disk Filesystem Format (ext2-like, 4KB blocks)
// ============================================================================

// Magic number to identify our filesystem
#define FS_MAGIC 0x534D4947u  /* 'SMIG' */
#define FS_VERSION 1u
#define FS_BLOCK_SIZE 4096
#define FS_BLOCK_BITS (FS_BLOCK_SIZE * 8)

// Sector layout (512-byte sectors):
#define FS_SUPERBLOCK_SECTOR 2
#define FS_INODE_BITMAP_SECTOR 3
#define FS_BLOCK_BITMAP_START_SECTOR 4
#define FS_BLOCK_BITMAP_SECTORS 2
#define FS_INODE_TABLE_START_SECTOR 10
#define FS_DATA_START_SECTOR 2010

// Inode and block counts
#define FS_TOTAL_INODES 4096
#define FS_TOTAL_BLOCKS 2048  // 2048 * 4KB = 8MB

// Superblock: 256 bytes, stored at sector 2
typedef struct __attribute__((packed)) {
    uint32_t magic;              // 0x534D4947 ('SMIG')
    uint32_t version;            // Version number
    uint32_t block_size;         // Bytes per block (4096)
    uint32_t inode_size;         // Bytes per inode (256)
    uint32_t total_blocks;       // Total data blocks available
    uint32_t total_inodes;       // Total inodes available
    uint32_t free_blocks;        // Number of free blocks
    uint32_t free_inodes;        // Number of free inodes
    uint32_t creation_time;      // Unix timestamp of creation
    uint32_t last_mount_time;    // Last mount time
    uint32_t mount_count;        // Number of mounts
    uint32_t max_mount_count;    // Max mounts before fsck (0 = never)
    uint32_t root_inode;         // Inode number of root directory (usually 1)
    uint32_t generation;         // Generation counter for safety
    uint8_t reserved[256 - 52];  // Padding to 256 bytes
} FSuperblock;

// Inode: 256 bytes, for both files and directories
typedef struct __attribute__((packed)) {
    uint16_t mode;               // File type + permissions (bit 15: dir, 0-14: rwx...)
    uint16_t uid;                // User ID owner
    uint32_t size;               // File size in bytes
    uint32_t atime;              // Access time
    uint32_t ctime;              // Change time
    uint32_t mtime;              // Modification time
    uint32_t dtime;              // Deletion time
    uint16_t gid;                // Group ID
    uint16_t link_count;         // Hard link count
    uint32_t disk_sectors;       // Sectors used (not relevant for us, info only)
    uint32_t flags;              // File flags
    uint32_t os_specific1;       // OS-specific (unused)
    
    // Block pointers (12 direct + 1 indirect)
    uint32_t direct_blocks[12];  // Direct block pointers
    uint32_t indirect_block;     // Single indirect block
    uint32_t dind_block;         // Double indirect (unused for now)
    uint32_t tind_block;         // Triple indirect (unused for now)
    
    uint32_t generation;         // File generation/version
    uint32_t ext_attributes;     // Extended attributes block
    uint32_t file_acl_high;      // High 32 bits of file ACL
    uint32_t author_uid;         // Author's UID
    
    uint8_t reserved[256 - 112]; // Padding to 256 bytes
} FInode;

// File mode bits
#define INODE_MODE_DIR   0x8000  // Directory
#define INODE_MODE_FILE  0x0000  // Regular file
#define INODE_PERM_OWNER_R 0x0100  // Owner read
#define INODE_PERM_OWNER_W 0x0080  // Owner write
#define INODE_PERM_OWNER_X 0x0040  // Owner execute
#define INODE_PERM_GROUP_R 0x0020  // Group read
#define INODE_PERM_GROUP_W 0x0010  // Group write
#define INODE_PERM_GROUP_X 0x0008  // Group execute
#define INODE_PERM_OTHERS_R 0x0004  // Others read
#define INODE_PERM_OTHERS_W 0x0002  // Others write
#define INODE_PERM_OTHERS_X 0x0001  // Others execute

// Directory entry: variable size, but typically 260 bytes to align nicely
#define DIRENT_NAME_MAX 251
typedef struct __attribute__((packed)) {
    uint32_t inode;              // Inode number
    uint16_t rec_len;            // Record length (256 typically)
    uint8_t name_len;            // Length of name
    uint8_t file_type;           // File type (1=file, 2=dir, etc)
    char name[252];              // Filename (null-terminated)
} DirectoryEntry;

// File type field values
#define DIRENT_TYPE_UNKNOWN 0
#define DIRENT_TYPE_FILE    1
#define DIRENT_TYPE_DIR     2

// ============================================================================
// VFS Abstraction Layer
// ============================================================================

// VFS operations function pointers
typedef struct VFS_Operations {
    int (*open)(const char* path, int flags);
    int (*close)(int fd);
    int (*read)(int fd, char* buf, int count);
    int (*write)(int fd, const char* buf, int count);
    int (*readdir)(int fd, DirectoryEntry* entries, int max_entries);
    int (*mkdir)(const char* path);
    int (*unlink)(const char* path);
} VFS_Operations;

// VFS mount entry
typedef struct {
    char mount_point[64];        // e.g., "/", "/proc", "/dev"
    VFS_Operations* ops;         // Function pointers for this mount
    void* private_data;          // Backend-specific data
    int used;
} VFS_Mount;

// ============================================================================
// RAM Cache Structures (for performance)
// ============================================================================

// Cached inode
typedef struct {
    uint32_t inode_num;
    FInode inode;
    int dirty;                   // Needs write to disk
    int used;
} CachedInode;

#define INODE_CACHE_SIZE 64

// Cached block
typedef struct {
    uint32_t block_num;
    uint8_t data[FS_BLOCK_SIZE];
    int dirty;
    int used;
} CachedBlock;

#define BLOCK_CACHE_SIZE 32

// ============================================================================
// Public Disk API (replaces old filesystem.c functions)
// ============================================================================

// Initialization
int disk_fs_init(void);
int disk_fs_format(void);

// Block operations
int disk_allocate_block(void);
void disk_free_block(uint32_t block_num);
int disk_read_block(uint32_t block, uint8_t* buf);
int disk_write_block(uint32_t block, const uint8_t* buf);

// Inode operations
int disk_read_inode(uint32_t inode_num, FInode* out);
int disk_write_inode(uint32_t inode_num, const FInode* inode);
uint32_t inode_allocate(void);
void inode_free(uint32_t inode_num);

// Inode block access
uint32_t inode_get_block(FInode* inode, uint32_t file_block_idx);
int inode_set_block(FInode* inode, uint32_t file_block_idx, uint32_t block_num);

// Path resolution
int fs_path_normalize(const char* in_path, char* out_path, int out_max);
int path_resolve(const char* path, FInode* inode_out);
int path_resolve_parent(const char* path, char* name_out, FInode* parent_out);

// Superblock operations
int disk_read_superblock(FSuperblock* out);
int disk_write_superblock(const FSuperblock* sb);

// ============================================================================
// VFS Public API (new abstraction layer)
// ============================================================================

int vfs_init(void);
int vfs_mount(const char* path, VFS_Operations* ops);
int vfs_open(const char* path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, char* buf, int count);
int vfs_write(int fd, const char* buf, int count);
int vfs_readdir(const char* path, DirectoryEntry* entries, int max_entries);
int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
int vfs_stat(const char* path, FInode* stat_out);
void vfs_close_for_pid(int pid);

// ============================================================================
// File Descriptor Structure (revised)
// ============================================================================

typedef struct {
    int used;
    int inode_num;               // Now references inode number on disk
    uint32_t offset;             // Current file offset
    int flags;                   // FS_O_READ, FS_O_WRITE, etc
    int owner_pid;               // Process that opened this
    int is_vfs_mount;            // Whether this uses VFS backend
    VFS_Operations* vfs_ops;     // VFS operations if needed
    void* vfs_private;           // VFS-specific file data
} KernelFD_NewFS;

#endif // FILESYSTEM_NEW_H
