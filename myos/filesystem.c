
#include "kernel.h"
#include <stdint.h>

// Simple memcpy for kernel use
static void* my_memcpy(void* dest, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

extern void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);

// Persistent filesystem state
#define FS_IMAGE_MAGIC 0x534D4947u
#define FS_IMAGE_VERSION 5u

#define FS_SLOT_COUNT 2
#define FS_SLOT_HEADER_SECTORS 1
#define FS_SLOT_TOTAL_SECTORS (FS_SECTOR_COUNT / FS_SLOT_COUNT)
#define FS_SLOT_DATA_SECTORS (FS_SLOT_TOTAL_SECTORS - FS_SLOT_HEADER_SECTORS)

#define FS_SLOT_MAGIC 0x534C4F54u /* 'SLOT' */
#define FS_SLOT_VERSION 1u

struct FSImage {
    uint32_t magic;
    uint32_t version;
    FSNode node_table[MAX_NODES];
    int node_count;
    int current_dir_idx;
    RamDir dir_table[MAX_DIRS];
    int dir_count;
    int current_dir;
    } __attribute__((packed));

typedef char fs_image_must_fit_in_reserved_sectors[
    (sizeof(struct FSImage) <= (FS_SLOT_DATA_SECTORS * 512)) ? 1 : -1
];

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t payload_size;
    uint32_t checksum;
} __attribute__((packed)) FSSlotHeader;

static struct FSImage fs_slot_temp;
static struct FSImage fs_image;
static uint32_t fs_active_generation = 0;

static void fsimage_to_globals(void);
static void globals_to_fsimage(void);

static uint32_t fs_checksum(const unsigned char* data, int len) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static int fs_slot_base_sector(int slot) {
    return FS_DISK_SECTOR + slot * FS_SLOT_TOTAL_SECTORS;
}

static int fs_read_slot_header(int slot, FSSlotHeader* header_out) {
    if (!header_out || slot < 0 || slot >= FS_SLOT_COUNT) return 0;
    unsigned char sector_buf[512];
    if (disk_read_sector(fs_slot_base_sector(slot), sector_buf) != 0) return 0;
    my_memcpy(header_out, sector_buf, sizeof(FSSlotHeader));
    if (header_out->magic != FS_SLOT_MAGIC) return 0;
    if (header_out->version != FS_SLOT_VERSION) return 0;
    if (header_out->payload_size != sizeof(struct FSImage)) return 0;
    return 1;
}

static int fs_read_slot_image(int slot, struct FSImage* image_out) {
    if (!image_out || slot < 0 || slot >= FS_SLOT_COUNT) return 0;
    unsigned char sector_buf[512];
    unsigned char* dst = (unsigned char*)image_out;

    for (int i = 0; i < FS_SLOT_DATA_SECTORS; i++) {
        if (disk_read_sector(fs_slot_base_sector(slot) + FS_SLOT_HEADER_SECTORS + i, sector_buf) != 0) {
            return 0;
        }
        int offset = i * 512;
        int to_copy = sizeof(struct FSImage) - offset;
        if (to_copy <= 0) break;
        if (to_copy > 512) to_copy = 512;
        my_memcpy(dst + offset, sector_buf, to_copy);
    }
    return 1;
}

static int fs_write_slot_image(int slot, const struct FSImage* image) {
    if (!image || slot < 0 || slot >= FS_SLOT_COUNT) return 0;
    unsigned char sector_buf[512];
    const unsigned char* src = (const unsigned char*)image;

    for (int i = 0; i < FS_SLOT_DATA_SECTORS; i++) {
        for (int j = 0; j < 512; j++) sector_buf[j] = 0;
        int offset = i * 512;
        int to_copy = sizeof(struct FSImage) - offset;
        if (to_copy <= 0) {
            if (disk_write_sector(fs_slot_base_sector(slot) + FS_SLOT_HEADER_SECTORS + i, sector_buf) != 0) {
                return 0;
            }
            continue;
        }
        if (to_copy > 512) to_copy = 512;
        my_memcpy(sector_buf, src + offset, to_copy);
        if (disk_write_sector(fs_slot_base_sector(slot) + FS_SLOT_HEADER_SECTORS + i, sector_buf) != 0) {
            return 0;
        }
    }
    return 1;
}

static int fs_write_slot_header(int slot, const FSSlotHeader* header) {
    if (!header || slot < 0 || slot >= FS_SLOT_COUNT) return 0;
    unsigned char sector_buf[512];
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;
    my_memcpy(sector_buf, header, sizeof(FSSlotHeader));
    return disk_write_sector(fs_slot_base_sector(slot), sector_buf) == 0;
}

static int fs_load_legacy_single_image(void) {
    unsigned char sector_buf[512];
    unsigned char* img_ptr = (unsigned char*)&fs_image;
    int ok = 1;
    for (int i = 0; i < FS_SLOT_DATA_SECTORS; i++) {
        if (disk_read_sector(FS_DISK_SECTOR + i, sector_buf) != 0) {
            ok = 0;
            break;
        }
        int offset = i * 512;
        int to_copy = sizeof(fs_image) - offset;
        if (to_copy <= 0) break;
        if (to_copy > 512) to_copy = 512;
        my_memcpy(img_ptr + offset, sector_buf, to_copy);
    }
    if (!ok) return 0;
    if (fs_image.magic != FS_IMAGE_MAGIC || fs_image.version != FS_IMAGE_VERSION) return 0;
    fsimage_to_globals();
    fs_active_generation = 0;
    return 1;
}

// NOTE: To check persistent image size, print sizeof(struct FSImage) in a test program or with a build-time assert in a C file where both struct FSImage and FS_SECTOR_COUNT are visible.

// Update global variables from fs_image
static void fsimage_to_globals() {
    my_memcpy(node_table, fs_image.node_table, sizeof(node_table));
    node_count = fs_image.node_count;
    current_dir_idx = fs_image.current_dir_idx;
    my_memcpy(dir_table, fs_image.dir_table, sizeof(dir_table));
    dir_count = fs_image.dir_count;
    current_dir = fs_image.current_dir;
}
// Update fs_image from global variables
static void globals_to_fsimage() {
    fs_image.magic = FS_IMAGE_MAGIC;
    fs_image.version = FS_IMAGE_VERSION;
    my_memcpy(fs_image.node_table, node_table, sizeof(node_table));
    fs_image.node_count = node_count;
    fs_image.current_dir_idx = current_dir_idx;
    my_memcpy(fs_image.dir_table, dir_table, sizeof(dir_table));
    fs_image.dir_count = dir_count;
    fs_image.current_dir = current_dir;
}

// --- Global Variables ---
FSNode node_table[MAX_NODES];
int node_count = 0;
int current_dir_idx = 0;

RamDir dir_table[MAX_DIRS] = { {"root", 1, -1} };
int dir_count = 1;
int current_dir = 0;

// --- String Utilities ---
int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int str_equal(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

int mini_strcmp(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return a[i] - b[i];
        i++;
    }
    return a[i] - b[i];
}

void int_to_str(int value, char* buf) {
    char temp[12];
    int i = 0, j = 0;
    int is_negative = value < 0;
    if (is_negative) value = -value;
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    if (is_negative) temp[i++] = '-';
    while (i > 0) buf[j++] = temp[--i];
    buf[j] = 0;
}

void str_concat(char* dest, const char* src) {
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = 0;
}

void init_filesystem() {
    for (int i = 0; i < MAX_NODES; i++) {
        node_table[i].used = 0;
        node_table[i].child_count = 0;
    }
    // Create root directory
    node_table[0].used = 1;
    node_table[0].type = NODE_DIRECTORY;
    node_table[0].parent_idx = -1;
    node_table[0].child_count = 0;
    node_table[0].name[0] = '/';
    node_table[0].name[1] = 0;
    node_count = 1;
    current_dir_idx = 0;
}

void get_full_path(int node_idx, char* path, int max_len) {
    if (node_idx < 0 || node_idx >= MAX_NODES || !node_table[node_idx].used) {
        path[0] = '/';
        path[1] = 0;
        return;
    }
    
    if (node_table[node_idx].parent_idx == -1) {
        path[0] = '/';
        path[1] = 0;
        return;
    }
    
    // Build path recursively
    char temp[MAX_PATH_LENGTH];
    int parts[32];
    int part_count = 0;
    int current = node_idx;
    
    while (current != -1 && current != 0 && part_count < 32) {
        parts[part_count++] = current;
        current = node_table[current].parent_idx;
    }
    
    path[0] = '/';
    int pos = 1;
    for (int i = part_count - 1; i >= 0 && pos < max_len - 1; i--) {
        const char* name = node_table[parts[i]].name;
        int j = 0;
        while (name[j] && pos < max_len - 1) {
            path[pos++] = name[j++];
        }
        if (i > 0 && pos < max_len - 1) {
            path[pos++] = '/';
        }
    }
    path[pos] = 0;
}

int parse_path(const char* path, char components[32][MAX_NAME_LENGTH], int* comp_count) {
    *comp_count = 0;
    int i = 0;
    int is_absolute = 0;
    
    // Skip leading spaces
    while (path[i] == ' ') i++;
    
    // Check if absolute path
    if (path[i] == '/') {
        is_absolute = 1;
        i++;
    }
    
    while (path[i]) {
        if (path[i] == ' ') break; // Stop at space
        
        if (path[i] == '/') {
            i++;
            continue;
        }
        
        // Read component
        int j = 0;
        while (path[i] && path[i] != '/' && path[i] != ' ' && j < MAX_NAME_LENGTH - 1) {
            components[*comp_count][j++] = path[i++];
        }
        components[*comp_count][j] = 0;
        
        if (j > 0) {
            (*comp_count)++;
            if (*comp_count >= 32) break;
        }
    }
    
    return is_absolute;
}

int resolve_path(const char* path) {
    char components[32][MAX_NAME_LENGTH];
    int comp_count = 0;
    int is_absolute = parse_path(path, components, &comp_count);
    
    int current = is_absolute ? 0 : current_dir_idx;
    
    for (int i = 0; i < comp_count; i++) {
        if (str_equal(components[i], ".")) {
            continue; // Stay in current directory
        } else if (str_equal(components[i], "..")) {
            if (node_table[current].parent_idx != -1) {
                current = node_table[current].parent_idx;
            }
        } else {
            // Find child with matching name
            int found = -1;
            for (int j = 0; j < node_table[current].child_count; j++) {
                int child_idx = node_table[current].children_idx[j];
                if (str_equal(node_table[child_idx].name, components[i])) {
                    found = child_idx;
                    break;
                }
            }
            
            if (found == -1) {
                return -1; // Path not found
            }
            current = found;
        }
    }
    
    return current;
}

int fs_mkdir(const char* path) {
    // Find parent directory
    char parent_path[MAX_PATH_LENGTH];
    char dirname[MAX_NAME_LENGTH];
    
    int path_len = str_len(path);
    int last_slash = -1;
    for (int i = path_len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }
    
    int parent_idx;
    if (last_slash <= 0) {
        parent_idx = path[0] == '/' ? 0 : current_dir_idx;
        str_copy(dirname, path[0] == '/' ? path + 1 : path, MAX_NAME_LENGTH);
    } else {
        for (int i = 0; i < last_slash; i++) parent_path[i] = path[i];
        parent_path[last_slash] = 0;
        parent_idx = resolve_path(parent_path);
        str_copy(dirname, path + last_slash + 1, MAX_NAME_LENGTH);
    }
    
    if (parent_idx == -1 || node_table[parent_idx].type != NODE_DIRECTORY) {
        return -1;
    }
    
    // Check if already exists
    for (int i = 0; i < node_table[parent_idx].child_count; i++) {
        int child_idx = node_table[parent_idx].children_idx[i];
        if (str_equal(node_table[child_idx].name, dirname)) {
            return -2; // Already exists
        }
    }
    
    // Find free node
    int new_idx = -1;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!node_table[i].used) {
            new_idx = i;
            break;
        }
    }
    
    if (new_idx == -1 || node_table[parent_idx].child_count >= MAX_CHILDREN) {
        return -3; // No space
    }
    
    // Create directory
    node_table[new_idx].used = 1;
    node_table[new_idx].type = NODE_DIRECTORY;
    node_table[new_idx].parent_idx = parent_idx;
    node_table[new_idx].child_count = 0;
    str_copy(node_table[new_idx].name, dirname, MAX_NAME_LENGTH);
    
    node_table[parent_idx].children_idx[node_table[parent_idx].child_count++] = new_idx;
    if (new_idx >= node_count) node_count = new_idx + 1;
    
    int ret = new_idx;
    fs_save();
    return ret;
}

int fs_touch(const char* path, const char* content) {
    char parent_path[MAX_PATH_LENGTH];
    char filename[MAX_NAME_LENGTH];
    
    int path_len = str_len(path);
    int last_slash = -1;
    for (int i = path_len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }
    
    int parent_idx;
    if (last_slash <= 0) {
        parent_idx = path[0] == '/' ? 0 : current_dir_idx;
        str_copy(filename, path[0] == '/' ? path + 1 : path, MAX_NAME_LENGTH);
    } else {
        for (int i = 0; i < last_slash; i++) parent_path[i] = path[i];
        parent_path[last_slash] = 0;
        parent_idx = resolve_path(parent_path);
        str_copy(filename, path + last_slash + 1, MAX_NAME_LENGTH);
    }
    
    if (parent_idx == -1) return -1;
    if (!node_table[parent_idx].used || node_table[parent_idx].type != NODE_DIRECTORY) return -1;
    
    // Check if already exists
    for (int i = 0; i < node_table[parent_idx].child_count; i++) {
        int child_idx = node_table[parent_idx].children_idx[i];
        if (str_equal(node_table[child_idx].name, filename)) {
            if (node_table[child_idx].type == NODE_FILE) {
                // Overwrite file content and update content_size
                if (content) {
                    int len = 0;
                    while (content[len] && len < MAX_FILE_CONTENT - 1) {
                        node_table[child_idx].content[len] = content[len];
                        len++;
                    }
                    node_table[child_idx].content[len] = 0;
                    node_table[child_idx].content_size = len;
                } else {
                    node_table[child_idx].content[0] = 0;
                    node_table[child_idx].content_size = 0;
                }
                fs_save();
                return child_idx;
            }
            return -2;
        }
    }
    
    int new_idx = -1;
    for (int i = 0; i < MAX_NODES; i++) {
        if (!node_table[i].used) {
            new_idx = i;
            break;
        }
    }
    
    if (new_idx == -1) {
        return -3;
    }
    if (node_table[parent_idx].child_count >= MAX_CHILDREN) {
        // Too many files in this directory
        return -4;
    }
    
    node_table[new_idx].used = 1;
    node_table[new_idx].type = NODE_FILE;
    node_table[new_idx].parent_idx = parent_idx;
    node_table[new_idx].child_count = 0;
    str_copy(node_table[new_idx].name, filename, MAX_NAME_LENGTH);
    
    if (content) {
        int len = 0;
        while (content[len] && len < MAX_FILE_CONTENT - 1) {
            node_table[new_idx].content[len] = content[len];
            len++;
        }
        node_table[new_idx].content[len] = 0;
        node_table[new_idx].content_size = len;
    } else {
        node_table[new_idx].content[0] = 0;
        node_table[new_idx].content_size = 0;
    }
    
    node_table[parent_idx].children_idx[node_table[parent_idx].child_count++] = new_idx;
    if (new_idx >= node_count) node_count = new_idx + 1;
    
    int ret = new_idx;
    fs_save();
    return ret;
}

int fs_rm(const char* path, int recursive) {
    int node_idx = resolve_path(path);
    if (node_idx == -1 || node_idx == 0) return -1;
    
    if (node_table[node_idx].type == NODE_DIRECTORY && !recursive && node_table[node_idx].child_count > 0) {
        return -2;
    }
    
    int parent_idx = node_table[node_idx].parent_idx;
    
    // Remove from parent's children
    for (int i = 0; i < node_table[parent_idx].child_count; i++) {
        if (node_table[parent_idx].children_idx[i] == node_idx) {
            for (int j = i; j < node_table[parent_idx].child_count - 1; j++) {
                node_table[parent_idx].children_idx[j] = node_table[parent_idx].children_idx[j + 1];
            }
            node_table[parent_idx].child_count--;
            break;
        }
    }
    
    // Recursively delete children if directory
    if (node_table[node_idx].type == NODE_DIRECTORY && recursive) {
        while (node_table[node_idx].child_count > 0) {
            int child_idx = node_table[node_idx].children_idx[0];
            char child_path[MAX_PATH_LENGTH];
            get_full_path(child_idx, child_path, MAX_PATH_LENGTH);
            fs_rm(child_path, 1);
        }
    }
    
    node_table[node_idx].used = 0;
    fs_save();
    return 0;
}


static int fs_try_load_slot(int slot, uint32_t* generation_out) {
    FSSlotHeader header;
    if (!generation_out) return 0;
    if (!fs_read_slot_header(slot, &header)) return 0;
    if (!fs_read_slot_image(slot, &fs_slot_temp)) return 0;

    uint32_t actual_checksum = fs_checksum((const unsigned char*)&fs_slot_temp, sizeof(struct FSImage));
    if (actual_checksum != header.checksum) return 0;
    if (fs_slot_temp.magic != FS_IMAGE_MAGIC || fs_slot_temp.version != FS_IMAGE_VERSION) return 0;

    my_memcpy(&fs_image, &fs_slot_temp, sizeof(struct FSImage));
    *generation_out = header.generation;
    return 1;
}

static int fs_slot_is_valid(int slot, uint32_t* generation_out) {
    FSSlotHeader header;
    if (!generation_out) return 0;
    if (!fs_read_slot_header(slot, &header)) return 0;
    if (!fs_read_slot_image(slot, &fs_slot_temp)) return 0;

    uint32_t actual_checksum = fs_checksum((const unsigned char*)&fs_slot_temp, sizeof(struct FSImage));
    if (actual_checksum != header.checksum) return 0;
    if (fs_slot_temp.magic != FS_IMAGE_MAGIC || fs_slot_temp.version != FS_IMAGE_VERSION) return 0;

    *generation_out = header.generation;
    return 1;
}

void fs_save() {
    globals_to_fsimage();

    int target_slot = (int)((fs_active_generation + 1u) & 1u);
    FSSlotHeader header;
    header.magic = FS_SLOT_MAGIC;
    header.version = FS_SLOT_VERSION;
    header.generation = fs_active_generation + 1u;
    header.payload_size = sizeof(struct FSImage);
    header.checksum = fs_checksum((const unsigned char*)&fs_image, sizeof(struct FSImage));

    if (!fs_write_slot_image(target_slot, &fs_image) || !fs_write_slot_header(target_slot, &header)) {
        volatile char* vga = (volatile char*)0xB8000;
        const char* msg = "Disk write error (non-fatal)!";
        for (int j = 0; msg[j]; j++) {
            vga[j*2] = msg[j];
            vga[j*2+1] = 0x4F;
        }
        return;
    }

    fs_active_generation = header.generation;
}

void fs_load() {
    uint32_t best_generation = 0;
    int found = 0;

    for (int slot = 0; slot < FS_SLOT_COUNT; slot++) {
        uint32_t gen = 0;
        if (fs_try_load_slot(slot, &gen)) {
            if (!found || gen > best_generation) {
                best_generation = gen;
                found = 1;
            }
        }
    }

    if (found) {
        fsimage_to_globals();
        fs_active_generation = best_generation;
        return;
    }

    if (fs_load_legacy_single_image()) {
        fs_save();
        return;
    }

    fs_active_generation = 0;
}

void fs_get_status(uint32_t* active_generation_out, int slot_validity[2]) {
    if (active_generation_out) {
        *active_generation_out = fs_active_generation;
    }
    if (!slot_validity) return;

    for (int i = 0; i < FS_SLOT_COUNT; i++) {
        uint32_t generation = 0;
        slot_validity[i] = fs_slot_is_valid(i, &generation) ? 1 : 0;
    }
}