// ============================================================================
// PHASE 2: Block Allocation and Disk I/O
// ============================================================================

#include "kernel.h"
#include "filesystem_new.h"
#include <stdint.h>

extern int inode_write_directory(uint32_t inode_num, const DirectoryEntry* entries, int entry_count);
extern int inode_create_directory(const char* path, uint16_t mode, uint16_t uid, uint16_t gid);

// Private cache structures
static CachedInode inode_cache[INODE_CACHE_SIZE] __attribute__((unused));
static int inode_cache_count __attribute__((unused)) = 0;

static CachedBlock block_cache[BLOCK_CACHE_SIZE] __attribute__((unused));
static int block_cache_count __attribute__((unused)) = 0;

// Superblock cache
static FSuperblock cached_superblock;
static int superblock_dirty = 0;

int disk_fs_flush(void);

// Bitmaps (allocated once at init, kept in memory for performance)
static uint8_t inode_bitmap[512];  // 4096 inodes = 512 bytes
static uint8_t block_bitmap[1024]; // 2048 blocks = 256 bytes (2 sectors)
static int bitmap_dirty = 0;

// disk_read_sector/disk_write_sector are declared in kernel.h

// ============================================================================
// Utility Functions
// ============================================================================

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

// ============================================================================
// Bitmap Operations (block and inode allocation)
// ============================================================================

// Set a bit in bitmap to 1 (mark as used)
static void bitmap_set_bit(uint8_t* bitmap, int bit_idx) {
    int byte_idx = bit_idx / 8;
    int bit_offset = bit_idx % 8;
    bitmap[byte_idx] |= (1 << bit_offset);
}

// Clear a bit in bitmap to 0 (mark as free)
static void bitmap_clear_bit(uint8_t* bitmap, int bit_idx) {
    int byte_idx = bit_idx / 8;
    int bit_offset = bit_idx % 8;
    bitmap[byte_idx] &= ~(1 << bit_offset);
}

// Check if a bit is set in bitmap
static int bitmap_is_set(const uint8_t* bitmap, int bit_idx) {
    int byte_idx = bit_idx / 8;
    int bit_offset = bit_idx % 8;
    return (bitmap[byte_idx] & (1 << bit_offset)) != 0;
}

// Find first free bit in bitmap (returns -1 if none)
static int bitmap_find_free(const uint8_t* bitmap, int max_bits) {
    for (int i = 0; i < max_bits; i++) {
        if (!bitmap_is_set(bitmap, i)) {
            return i;
        }
    }
    return -1;
}

// Count free bits in bitmap
static int __attribute__((unused)) bitmap_count_free(const uint8_t* bitmap, int max_bits) {
    int count = 0;
    for (int i = 0; i < max_bits; i++) {
        if (!bitmap_is_set(bitmap, i)) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Sector-to-Block Conversion
// ============================================================================

// Convert block number to starting sector
static int block_to_sector(uint32_t block_num) {
    if (block_num >= FS_TOTAL_BLOCKS) return -1;
    return FS_DATA_START_SECTOR + (block_num * (FS_BLOCK_SIZE / 512));
}

// Convert sector to block number (returns -1 if not block aligned)
static int __attribute__((unused)) sector_to_block(int sector) {
    if (sector < FS_DATA_START_SECTOR) return -1;
    int offset = sector - FS_DATA_START_SECTOR;
    if (offset % (FS_BLOCK_SIZE / 512) != 0) return -1;
    return offset / (FS_BLOCK_SIZE / 512);
}

// ============================================================================
// Disk I/O Operations
// ============================================================================

// Read a single 4KB block
int disk_read_block(uint32_t block, uint8_t* buf) {
    if (!buf || block >= FS_TOTAL_BLOCKS) return -1;
    
    int start_sector = block_to_sector(block);
    if (start_sector < 0) return -1;
    
    // A 4KB block is 8 sectors (512 bytes each)
    for (int i = 0; i < 8; i++) {
        if (disk_read_sector(start_sector + i, buf + (i * 512)) != 0) {
            return -1;
        }
    }
    return 0;
}

// Write a single 4KB block
int disk_write_block(uint32_t block, const uint8_t* buf) {
    if (!buf || block >= FS_TOTAL_BLOCKS) return -1;
    
    int start_sector = block_to_sector(block);
    if (start_sector < 0) return -1;
    
    // A 4KB block is 8 sectors
    for (int i = 0; i < 8; i++) {
        if (disk_write_sector(start_sector + i, (unsigned char*)(buf + (i * 512))) != 0) {
            return -1;
        }
    }
    return 0;
}

// ============================================================================
// Bitmap I/O Operations
// ============================================================================

// Load inode bitmap from disk
static int load_inode_bitmap(void) {
    unsigned char sector_buf[512];
    if (disk_read_sector(FS_INODE_BITMAP_SECTOR, sector_buf) != 0) {
        return -1;
    }
    my_memcpy(inode_bitmap, sector_buf, 512);
    return 0;
}

// Save inode bitmap to disk
static int save_inode_bitmap(void) {
    if (disk_write_sector(FS_INODE_BITMAP_SECTOR, inode_bitmap) != 0) {
        return -1;
    }
    return 0;
}

// Load block bitmap from disk (2 sectors)
static int load_block_bitmap(void) {
    unsigned char sector_buf[512];
    
    if (disk_read_sector(FS_BLOCK_BITMAP_START_SECTOR, sector_buf) != 0) {
        return -1;
    }
    my_memcpy(block_bitmap, sector_buf, 512);
    
    if (disk_read_sector(FS_BLOCK_BITMAP_START_SECTOR + 1, sector_buf) != 0) {
        return -1;
    }
    my_memcpy(block_bitmap + 512, sector_buf, 512);
    
    return 0;
}

// Save block bitmap to disk (2 sectors)
static int save_block_bitmap(void) {
    unsigned char sector_buf[512];
    
    my_memcpy(sector_buf, block_bitmap, 512);
    if (disk_write_sector(FS_BLOCK_BITMAP_START_SECTOR, sector_buf) != 0) {
        return -1;
    }
    
    my_memcpy(sector_buf, block_bitmap + 512, 512);
    if (disk_write_sector(FS_BLOCK_BITMAP_START_SECTOR + 1, sector_buf) != 0) {
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Superblock Operations
// ============================================================================

// Read superblock from disk
int disk_read_superblock(FSuperblock* out) {
    if (!out) return -1;
    
    unsigned char sector_buf[512];
    if (disk_read_sector(FS_SUPERBLOCK_SECTOR, sector_buf) != 0) {
        return -1;
    }
    
    my_memcpy(out, sector_buf, sizeof(FSuperblock));
    
    // Verify magic number
    if (out->magic != FS_MAGIC) {
        return -1;
    }
    
    return 0;
}

// Write superblock to disk
int disk_write_superblock(const FSuperblock* sb) {
    if (!sb) return -1;
    
    if (sb->magic != FS_MAGIC) {
        return -1;
    }
    
    unsigned char sector_buf[512];
    my_memset(sector_buf, 0, 512);
    my_memcpy(sector_buf, (const unsigned char*)sb, sizeof(FSuperblock));
    
    if (disk_write_sector(FS_SUPERBLOCK_SECTOR, sector_buf) != 0) {
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Block Allocation
// ============================================================================

// Allocate a free block, return block number or -1 if none available
int disk_allocate_block(void) {
    int block_num = bitmap_find_free(block_bitmap, FS_TOTAL_BLOCKS);
    
    if (block_num < 0) {
        return -1;  // No free blocks
    }
    
    bitmap_set_bit(block_bitmap, block_num);
    bitmap_dirty = 1;
    
    // Update superblock
    if (cached_superblock.free_blocks > 0) {
        cached_superblock.free_blocks--;
    }
    superblock_dirty = 1;
    
    return block_num;
}

// Free a block
void disk_free_block(uint32_t block_num) {
    if (block_num >= FS_TOTAL_BLOCKS) return;
    
    if (bitmap_is_set(block_bitmap, block_num)) {
        bitmap_clear_bit(block_bitmap, block_num);
        bitmap_dirty = 1;
        
        // Update superblock
        if (cached_superblock.free_blocks < cached_superblock.total_blocks) {
            cached_superblock.free_blocks++;
        }
        superblock_dirty = 1;
    }
}

// ============================================================================
// Inode Allocation
// ============================================================================

// Allocate a free inode, return inode number or -1 if none available
uint32_t inode_allocate(void) {
    int inode_num = bitmap_find_free(inode_bitmap, FS_TOTAL_INODES);
    
    if (inode_num < 0) {
        return (uint32_t)-1;  // No free inodes
    }
    
    bitmap_set_bit(inode_bitmap, inode_num);
    bitmap_dirty = 1;
    
    // Update superblock
    if (cached_superblock.free_inodes > 0) {
        cached_superblock.free_inodes--;
    }
    superblock_dirty = 1;
    
    return (uint32_t)inode_num;
}

// Free an inode
void inode_free(uint32_t inode_num) {
    if (inode_num >= FS_TOTAL_INODES) return;
    
    if (bitmap_is_set(inode_bitmap, inode_num)) {
        bitmap_clear_bit(inode_bitmap, inode_num);
        bitmap_dirty = 1;
        
        // Update superblock
        if (cached_superblock.free_inodes < cached_superblock.total_inodes) {
            cached_superblock.free_inodes++;
        }
        superblock_dirty = 1;
    }
}

// ============================================================================
// Inode I/O Operations
// ============================================================================

// Read inode from disk
int disk_read_inode(uint32_t inode_num, FInode* out) {
    if (!out || inode_num >= FS_TOTAL_INODES) return -1;
    
    unsigned char sector_buf[512];
    
    // Each inode is 256 bytes, 2 per sector
    int sector_offset = inode_num / 2;
    int inode_offset = (inode_num % 2) * 256;
    
    int sector = FS_INODE_TABLE_START_SECTOR + sector_offset;
    
    if (disk_read_sector(sector, sector_buf) != 0) {
        return -1;
    }
    
    my_memcpy(out, sector_buf + inode_offset, sizeof(FInode));
    return 0;
}

// Write inode to disk
int disk_write_inode(uint32_t inode_num, const FInode* inode) {
    if (!inode || inode_num >= FS_TOTAL_INODES) return -1;
    
    unsigned char sector_buf[512];
    
    // Each inode is 256 bytes, 2 per sector
    int sector_offset = inode_num / 2;
    int inode_offset = (inode_num % 2) * 256;
    
    int sector = FS_INODE_TABLE_START_SECTOR + sector_offset;
    
    // Read the sector first to preserve the other inode
    if (disk_read_sector(sector, sector_buf) != 0) {
        return -1;
    }
    
    // Update the inode in the buffer
    my_memcpy(sector_buf + inode_offset, (const unsigned char*)inode, sizeof(FInode));
    
    // Write back
    if (disk_write_sector(sector, sector_buf) != 0) {
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Inode Block Management
// ============================================================================

// Get the block number for a given file block index
// Handles direct blocks and single indirection
uint32_t inode_get_block(FInode* inode, uint32_t file_block_idx) {
    if (!inode) return (uint32_t)-1;
    
    // Direct blocks (0-11)
    if (file_block_idx < 12) {
        return inode->direct_blocks[file_block_idx];
    }
    
    // Single indirect block
    file_block_idx -= 12;
    if (file_block_idx < 1024) {  // 1024 entries per block
        unsigned char indirect_buf[FS_BLOCK_SIZE];
        if (disk_read_block(inode->indirect_block, indirect_buf) != 0) {
            return (uint32_t)-1;
        }
        
        uint32_t* entries = (uint32_t*)indirect_buf;
        return entries[file_block_idx];
    }
    
    return (uint32_t)-1;  // File too large or not supported
}

// Set the block number for a given file block index
int inode_set_block(FInode* inode, uint32_t file_block_idx, uint32_t block_num) {
    if (!inode) return -1;
    
    // Direct blocks (0-11)
    if (file_block_idx < 12) {
        inode->direct_blocks[file_block_idx] = block_num;
        return 0;
    }
    
    // Single indirect block
    file_block_idx -= 12;
    if (file_block_idx < 1024) {
        unsigned char indirect_buf[FS_BLOCK_SIZE];
        
        // Allocate indirect block if needed
        if (inode->indirect_block == 0) {
            int new_block = disk_allocate_block();
            if (new_block < 0) return -1;
            
            inode->indirect_block = new_block;
            my_memset(indirect_buf, 0, FS_BLOCK_SIZE);
        } else {
            if (disk_read_block(inode->indirect_block, indirect_buf) != 0) {
                return -1;
            }
        }
        
        uint32_t* entries = (uint32_t*)indirect_buf;
        entries[file_block_idx] = block_num;
        
        if (disk_write_block(inode->indirect_block, indirect_buf) != 0) {
            return -1;
        }
        
        return 0;
    }
    
    return -1;  // File too large or not supported
}

// ============================================================================
// Filesystem Initialization
// ============================================================================

int disk_fs_init(void) {
    // Load superblock
    if (disk_read_superblock(&cached_superblock) != 0) {
        return -1;
    }
    
    // Load bitmaps
    if (load_inode_bitmap() != 0) {
        return -1;
    }
    
    if (load_block_bitmap() != 0) {
        return -1;
    }
    
    return 0;
}

// Format filesystem (DESTRUCTIVE - erases everything)
int disk_fs_format(void) {
    unsigned char sector_buf[512];
    unsigned char block_buf[FS_BLOCK_SIZE];
    
    // Initialize superblock
    my_memset(&cached_superblock, 0, sizeof(FSuperblock));
    cached_superblock.magic = FS_MAGIC;
    cached_superblock.version = FS_VERSION;
    cached_superblock.block_size = FS_BLOCK_SIZE;
    cached_superblock.inode_size = sizeof(FInode);
    cached_superblock.total_blocks = FS_TOTAL_BLOCKS;
    cached_superblock.total_inodes = FS_TOTAL_INODES;
    cached_superblock.free_blocks = FS_TOTAL_BLOCKS;
    cached_superblock.free_inodes = FS_TOTAL_INODES;
    cached_superblock.root_inode = 1;  // Root is inode 1
    cached_superblock.mount_count = 0;
    cached_superblock.max_mount_count = 0;
    
    // Write superblock
    if (disk_write_superblock(&cached_superblock) != 0) {
        return -1;
    }
    
    // Clear and write bitmaps (all free)
    my_memset(inode_bitmap, 0, 512);
    my_memset(block_bitmap, 0, 1024);
    
    // Mark inode 0 and 1 as used (reserved)
    bitmap_set_bit(inode_bitmap, 0);
    bitmap_set_bit(inode_bitmap, 1);
    
    if (save_inode_bitmap() != 0) {
        return -1;
    }
    
    if (save_block_bitmap() != 0) {
        return -1;
    }

    // Clear all inode table slots
    my_memset(sector_buf, 0, 512);
    for (int i = FS_INODE_TABLE_START_SECTOR; i < FS_INODE_TABLE_START_SECTOR + 2000; i++) {
        disk_write_sector(i, sector_buf);
    }
    
    // Clear all data blocks
    my_memset(block_buf, 0, FS_BLOCK_SIZE);
    for (int i = 0; i < FS_TOTAL_BLOCKS; i++) {
        disk_write_block(i, block_buf);
    }

    // Bootstrap a usable root directory after the wipe.
    FInode root_inode;
    DirectoryEntry root_entries[2];

    my_memset(&root_inode, 0, sizeof(FInode));
    root_inode.mode = INODE_MODE_DIR | INODE_PERM_OWNER_R | INODE_PERM_OWNER_W | INODE_PERM_OWNER_X;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.link_count = 2;
    root_inode.size = 0;
    if (disk_write_inode(1, &root_inode) != 0) {
        return -1;
    }

    my_memset(&root_entries[0], 0, sizeof(DirectoryEntry));
    root_entries[0].inode = 1;
    root_entries[0].file_type = DIRENT_TYPE_DIR;
    root_entries[0].name_len = 1;
    root_entries[0].name[0] = '.';

    my_memset(&root_entries[1], 0, sizeof(DirectoryEntry));
    root_entries[1].inode = 1;
    root_entries[1].file_type = DIRENT_TYPE_DIR;
    root_entries[1].name_len = 2;
    root_entries[1].name[0] = '.';
    root_entries[1].name[1] = '.';

    if (inode_write_directory(1, root_entries, 2) != 0) {
        return -1;
    }

    if (inode_create_directory("/tmp", INODE_PERM_OWNER_R | INODE_PERM_OWNER_W | INODE_PERM_OWNER_X, 0, 0) < 0) {
        return -1;
    }

    if (disk_fs_flush() != 0) {
        return -1;
    }
    
    return 0;
}

// Flush all dirty data to disk
int disk_fs_flush(void) {
    int ret = 0;
    
    if (superblock_dirty) {
        if (disk_write_superblock(&cached_superblock) != 0) {
            ret = -1;
        }
        superblock_dirty = 0;
    }
    
    if (bitmap_dirty) {
        if (save_inode_bitmap() != 0 || save_block_bitmap() != 0) {
            ret = -1;
        }
        bitmap_dirty = 0;
    }
    
    return ret;
}
