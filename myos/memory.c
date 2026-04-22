// Basic paging and physical frame allocator for Smiggles OS
// Inspired by Linux's virtual memory concepts:
// - paged virtual memory over physical frames
// - simple physical page allocator for kernel use

#include "kernel.h"

#define PAGE_SIZE          4096
#define FIRST_MANAGED_PHYS 0x00100000           // Start managing at 1 MiB
#define FALLBACK_MEMORY_TOP (16 * 1024 * 1024)

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u
#define MB2_TAG_TYPE_END 0u
#define MB2_TAG_TYPE_MMAP 6u

#define MAX_TRACKED_FRAMES (1024u * 1024u)      // Covers up to 4 GiB
#define MAX_BITMAP_WORDS   (MAX_TRACKED_FRAMES / 32u)

#define PAGE_4M_SIZE       (4u * 1024u * 1024u)

#define PAGE_FLAG_PRESENT  0x001u
#define PAGE_FLAG_RW       0x002u
#define PAGE_FLAG_USER     0x004u
#define PAGE_ADDR_MASK     0xFFFFF000u

#define MAX_BUDDY_ORDER    20u

#define SLAB_MAGIC         0x514C4142u
#define SLAB_FREE_NONE     0xFFFFu
#define SLAB_CLASS_COUNT   7

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

#define FIRST_MANAGED_FRAME   (FIRST_MANAGED_PHYS / PAGE_SIZE)

// Frame allocation bitmap: 1 = used, 0 = free
static uint32_t frame_bitmap[MAX_BITMAP_WORDS];
static uint32_t frame_refcount[MAX_TRACKED_FRAMES];
static uint32_t total_frames;
static uint32_t bitmap_words;
static uint32_t tracked_memory_top;

// Kernel master page directory and tracked mapped PDE count.
static uint32_t* kernel_page_directory;
static uint32_t mapped_pde_count;

static int32_t buddy_free_head[MAX_BUDDY_ORDER + 1u];
static int32_t buddy_next[MAX_TRACKED_FRAMES];
static int32_t buddy_prev[MAX_TRACKED_FRAMES];
static int8_t buddy_order_map[MAX_TRACKED_FRAMES];
static uint8_t buddy_is_free[MAX_TRACKED_FRAMES];
static uint32_t buddy_base_frame;
static uint32_t buddy_total_frames;
static uint32_t buddy_max_order;

typedef struct {
    uint32_t magic;
    uint16_t class_idx;
    uint16_t obj_size;
    uint16_t capacity;
    uint16_t free_count;
    uint16_t free_head;
    uint16_t reserved;
    uint32_t next;
} __attribute__((packed)) SlabPage;

typedef struct {
    uint16_t obj_size;
    uint32_t head;
} SlabClass;

static SlabClass slab_classes[SLAB_CLASS_COUNT];
static const uint16_t slab_sizes[SLAB_CLASS_COUNT] = {32u, 64u, 128u, 256u, 512u, 1024u, 2048u};

extern uint8_t __bss_end;

static inline void set_frame(uint32_t frame) {
    if (frame >= total_frames) return;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    if (idx >= bitmap_words) return;
    frame_bitmap[idx] |= (1U << bit);
}

static inline void clear_frame(uint32_t frame) {
    if (frame >= total_frames) return;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    if (idx >= bitmap_words) return;
    frame_bitmap[idx] &= ~(1U << bit);
}

static inline int frame_is_set(uint32_t frame) {
    if (frame >= total_frames) return 1;
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    if (idx >= bitmap_words) return 1;
    return (frame_bitmap[idx] & (1U << bit)) != 0;
}

static inline uint32_t phys_to_frame(uint32_t phys) {
    return phys / PAGE_SIZE;
}

void paging_inc_page_ref(unsigned int phys_addr) {
    uint32_t frame = phys_to_frame(phys_addr);
    if (frame >= total_frames) return;
    frame_refcount[frame]++;
}

unsigned int paging_get_page_ref(unsigned int phys_addr) {
    uint32_t frame = phys_to_frame(phys_addr);
    if (frame >= total_frames) return 0;
    return frame_refcount[frame];
}

void paging_dec_page_ref(unsigned int phys_addr) {
    uint32_t frame = phys_to_frame(phys_addr);
    if (frame >= total_frames) return;
    if (frame_refcount[frame] > 0) frame_refcount[frame]--;
}

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static uint32_t align_down_u32(uint32_t value, uint32_t align) {
    return value & ~(align - 1u);
}

static void zero_page_u32(uint32_t* page_u32) {
    for (int i = 0; i < 1024; i++) {
        page_u32[i] = 0;
    }
}

static uint32_t* alloc_zeroed_page(void) {
    uint32_t* page = (uint32_t*)alloc_page();
    if (!page) return 0;
    zero_page_u32(page);
    return page;
}

static void bitmap_mark_range(uint32_t start_frame, uint32_t count, int used) {
    for (uint32_t i = 0; i < count; i++) {
        if (used) set_frame(start_frame + i);
        else clear_frame(start_frame + i);
    }
}

static void buddy_list_push(uint32_t rel, uint32_t order) {
    int32_t head = buddy_free_head[order];
    buddy_next[rel] = head;
    buddy_prev[rel] = -1;
    if (head >= 0) buddy_prev[(uint32_t)head] = (int32_t)rel;
    buddy_free_head[order] = (int32_t)rel;
    buddy_order_map[rel] = (int8_t)order;
    buddy_is_free[rel] = 1;
}

static void buddy_list_remove(uint32_t rel, uint32_t order) {
    int32_t prev = buddy_prev[rel];
    int32_t next = buddy_next[rel];
    if (prev >= 0) buddy_next[(uint32_t)prev] = next;
    else buddy_free_head[order] = next;
    if (next >= 0) buddy_prev[(uint32_t)next] = prev;
    buddy_next[rel] = -1;
    buddy_prev[rel] = -1;
    buddy_is_free[rel] = 0;
}

static uint32_t buddy_list_pop(uint32_t order, int* ok) {
    int32_t head = buddy_free_head[order];
    if (head < 0) {
        *ok = 0;
        return 0;
    }
    buddy_list_remove((uint32_t)head, order);
    *ok = 1;
    return (uint32_t)head;
}

static void buddy_free_block(uint32_t rel, uint32_t order) {
    uint32_t block = rel;
    uint32_t cur_order = order;

    while (cur_order < buddy_max_order) {
        uint32_t buddy = block ^ (1u << cur_order);
        if (buddy >= buddy_total_frames) break;
        if (!buddy_is_free[buddy]) break;
        if ((uint32_t)buddy_order_map[buddy] != cur_order) break;

        buddy_list_remove(buddy, cur_order);
        if (buddy < block) block = buddy;
        cur_order++;
    }

    buddy_list_push(block, cur_order);
}

static int buddy_alloc_block(uint32_t order, uint32_t* out_rel) {
    if (order > buddy_max_order) return -1;

    uint32_t found = order;
    int ok = 0;
    while (found <= buddy_max_order) {
        if (buddy_free_head[found] >= 0) {
            ok = 1;
            break;
        }
        found++;
    }
    if (!ok) return -1;

    uint32_t rel = buddy_list_pop(found, &ok);
    if (!ok) return -1;

    while (found > order) {
        found--;
        uint32_t split = rel + (1u << found);
        buddy_list_push(split, found);
    }

    buddy_order_map[rel] = (int8_t)order;
    buddy_is_free[rel] = 0;
    *out_rel = rel;
    return 0;
}

static void init_buddy_allocator(void) {
    for (uint32_t i = 0; i <= MAX_BUDDY_ORDER; i++) {
        buddy_free_head[i] = -1;
    }

    buddy_base_frame = FIRST_MANAGED_FRAME;
    if (total_frames <= buddy_base_frame) {
        buddy_total_frames = 0;
        buddy_max_order = 0;
        return;
    }

    buddy_total_frames = total_frames - buddy_base_frame;
    buddy_max_order = 0;
    while (buddy_max_order < MAX_BUDDY_ORDER &&
           (1u << (buddy_max_order + 1u)) <= buddy_total_frames) {
        buddy_max_order++;
    }

    for (uint32_t i = 0; i < buddy_total_frames; i++) {
        buddy_next[i] = -1;
        buddy_prev[i] = -1;
        buddy_order_map[i] = -1;
        buddy_is_free[i] = 0;
    }

    for (uint32_t rel = 0; rel < buddy_total_frames; rel++) {
        uint32_t frame = buddy_base_frame + rel;
        if (!frame_is_set(frame)) {
            buddy_free_block(rel, 0);
        }
    }
}

static void slab_init(void) {
    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; i++) {
        slab_classes[i].obj_size = slab_sizes[i];
        slab_classes[i].head = 0;
    }
}

static uint32_t slab_page_data_offset(void) {
    return align_up_u32((uint32_t)sizeof(SlabPage), 8u);
}

static int slab_refill_class(uint32_t class_idx) {
    void* page = alloc_page();
    if (!page) return -1;

    SlabPage* slab = (SlabPage*)page;
    uint32_t data_off = slab_page_data_offset();
    uint16_t obj_size = slab_classes[class_idx].obj_size;
    uint16_t capacity = (uint16_t)((PAGE_SIZE - data_off) / obj_size);
    if (capacity == 0) {
        free_page(page);
        return -1;
    }

    slab->magic = SLAB_MAGIC;
    slab->class_idx = (uint16_t)class_idx;
    slab->obj_size = obj_size;
    slab->capacity = capacity;
    slab->free_count = capacity;
    slab->free_head = 0;
    slab->reserved = 0;
    slab->next = slab_classes[class_idx].head;

    uint8_t* base = (uint8_t*)page + data_off;
    for (uint16_t i = 0; i < capacity; i++) {
        uint16_t* next_idx = (uint16_t*)(base + (uint32_t)i * obj_size);
        *next_idx = (i + 1u < capacity) ? (uint16_t)(i + 1u) : SLAB_FREE_NONE;
    }

    slab_classes[class_idx].head = (uint32_t)(uintptr_t)page;
    return 0;
}

static void clear_usable_range(uint64_t start, uint64_t end) {
    if (end <= start) return;
    if (start >= (uint64_t)tracked_memory_top) return;
    if (end > (uint64_t)tracked_memory_top) end = tracked_memory_top;

    uint32_t first = (uint32_t)(start / PAGE_SIZE);
    uint32_t last = (uint32_t)((end - 1u) / PAGE_SIZE);
    if (last >= total_frames) last = total_frames - 1u;

    for (uint32_t f = first; f <= last; f++) {
        clear_frame(f);
    }
}

static void reserve_range(uint32_t start, uint32_t end) {
    if (end <= start) return;
    if (start >= tracked_memory_top) return;
    if (end > tracked_memory_top) end = tracked_memory_top;

    uint32_t first = start / PAGE_SIZE;
    uint32_t last = (end - 1u) / PAGE_SIZE;
    if (last >= total_frames) last = total_frames - 1u;

    for (uint32_t f = first; f <= last; f++) {
        set_frame(f);
    }
}

static int parse_multiboot2_mmap(uint32_t mb_magic, uint32_t mb_info_addr) {
    uint32_t max_usable_end = FALLBACK_MEMORY_TOP;
    int found_mmap = 0;

    if (mb_magic != MULTIBOOT2_BOOTLOADER_MAGIC || mb_info_addr == 0) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
        return 0;
    }

    const struct mb2_info* info = (const struct mb2_info*)(uintptr_t)mb_info_addr;
    uint32_t total_size = info->total_size;
    if (total_size < 16) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
        return 0;
    }

    const uint8_t* cursor = (const uint8_t*)info + 8;
    const uint8_t* end = (const uint8_t*)info + total_size;

    while (cursor + sizeof(struct mb2_tag) <= end) {
        const struct mb2_tag* tag = (const struct mb2_tag*)cursor;
        if (tag->size < sizeof(struct mb2_tag)) break;

        if (tag->type == MB2_TAG_TYPE_END) {
            break;
        }

        if (tag->type == MB2_TAG_TYPE_MMAP && tag->size >= sizeof(struct mb2_tag_mmap)) {
            const struct mb2_tag_mmap* mmap_tag = (const struct mb2_tag_mmap*)tag;
            const uint8_t* entry_ptr = (const uint8_t*)mmap_tag + sizeof(struct mb2_tag_mmap);
            const uint8_t* tag_end = (const uint8_t*)tag + tag->size;

            found_mmap = 1;
            while (entry_ptr + mmap_tag->entry_size <= tag_end &&
                   mmap_tag->entry_size >= sizeof(struct mb2_mmap_entry)) {
                const struct mb2_mmap_entry* ent = (const struct mb2_mmap_entry*)entry_ptr;
                if (ent->type == 1 && ent->length != 0) {
                    uint64_t usable_end64 = ent->base_addr + ent->length;
                    if (usable_end64 > 0xFFFFFFFFu) usable_end64 = 0xFFFFFFFFu;
                    if ((uint32_t)usable_end64 > max_usable_end) {
                        max_usable_end = (uint32_t)usable_end64;
                    }
                }
                entry_ptr += mmap_tag->entry_size;
            }
        }

        cursor += align_up_u32(tag->size, 8u);
    }

    tracked_memory_top = align_up_u32(max_usable_end, PAGE_SIZE);
    if (tracked_memory_top < FIRST_MANAGED_PHYS) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
    }

    if (tracked_memory_top > (MAX_TRACKED_FRAMES * PAGE_SIZE)) {
        tracked_memory_top = MAX_TRACKED_FRAMES * PAGE_SIZE;
    }

    if (!found_mmap) {
        tracked_memory_top = FALLBACK_MEMORY_TOP;
    }
    return found_mmap;
}

// Initialize frame bitmap state from multiboot memory map and reserved ranges.
static void init_frame_allocator(uint32_t mb_magic, uint32_t mb_info_addr) {
    int has_mmap = parse_multiboot2_mmap(mb_magic, mb_info_addr);
    total_frames = tracked_memory_top / PAGE_SIZE;
    if (total_frames == 0) {
        total_frames = FALLBACK_MEMORY_TOP / PAGE_SIZE;
        tracked_memory_top = FALLBACK_MEMORY_TOP;
    }

    bitmap_words = (total_frames + 31u) / 32u;
    if (bitmap_words > MAX_BITMAP_WORDS) {
        bitmap_words = MAX_BITMAP_WORDS;
        total_frames = MAX_TRACKED_FRAMES;
        tracked_memory_top = MAX_TRACKED_FRAMES * PAGE_SIZE;
    }

    for (uint32_t i = 0; i < bitmap_words; i++) {
        frame_bitmap[i] = 0xFFFFFFFFu;
    }
    for (uint32_t i = 0; i < total_frames; i++) {
        frame_refcount[i] = 0u;
    }

    if (has_mmap && mb_magic == MULTIBOOT2_BOOTLOADER_MAGIC && mb_info_addr != 0) {
        const struct mb2_info* info = (const struct mb2_info*)(uintptr_t)mb_info_addr;
        const uint8_t* cursor = (const uint8_t*)info + 8;
        const uint8_t* end = (const uint8_t*)info + info->total_size;

        while (cursor + sizeof(struct mb2_tag) <= end) {
            const struct mb2_tag* tag = (const struct mb2_tag*)cursor;
            if (tag->size < sizeof(struct mb2_tag)) break;
            if (tag->type == MB2_TAG_TYPE_END) break;

            if (tag->type == MB2_TAG_TYPE_MMAP && tag->size >= sizeof(struct mb2_tag_mmap)) {
                const struct mb2_tag_mmap* mmap_tag = (const struct mb2_tag_mmap*)tag;
                const uint8_t* entry_ptr = (const uint8_t*)mmap_tag + sizeof(struct mb2_tag_mmap);
                const uint8_t* tag_end = (const uint8_t*)tag + tag->size;

                while (entry_ptr + mmap_tag->entry_size <= tag_end &&
                       mmap_tag->entry_size >= sizeof(struct mb2_mmap_entry)) {
                    const struct mb2_mmap_entry* ent = (const struct mb2_mmap_entry*)entry_ptr;
                    if (ent->type == 1 && ent->length != 0) {
                        clear_usable_range(ent->base_addr, ent->base_addr + ent->length);
                    }
                    entry_ptr += mmap_tag->entry_size;
                }
            }

            cursor += align_up_u32(tag->size, 8u);
        }
    } else {
        clear_usable_range(FIRST_MANAGED_PHYS, tracked_memory_top);
    }

    // Keep BIOS area and low memory reserved permanently.
    reserve_range(0, FIRST_MANAGED_PHYS);

    // Keep kernel image, bss, and bootstrap stack reserved.
    reserve_range(0, align_up_u32((uint32_t)(uintptr_t)&__bss_end, PAGE_SIZE));

    // Keep Multiboot info payload reserved.
    if (mb_magic == MULTIBOOT2_BOOTLOADER_MAGIC && mb_info_addr != 0) {
        const struct mb2_info* info = (const struct mb2_info*)(uintptr_t)mb_info_addr;
        uint32_t mb_start = mb_info_addr;
        uint32_t mb_end = align_up_u32(mb_info_addr + info->total_size, PAGE_SIZE);
        reserve_range(mb_start, mb_end);
    }

    // Never allocate trailing bits in the final bitmap word.
    if ((total_frames & 31u) != 0) {
        uint32_t tail_start = total_frames & ~31u;
        for (uint32_t f = total_frames; f < tail_start + 32u; f++) {
            set_frame(f);
        }
    }

    for (uint32_t f = 0; f < total_frames; f++) {
        frame_refcount[f] = frame_is_set(f) ? 1u : 0u;
    }
}

static void destroy_process_directory_internal(uint32_t* pd) {
    if (!pd) return;
    for (uint32_t i = 0; i < mapped_pde_count; i++) {
        if ((pd[i] & PAGE_FLAG_PRESENT) == 0) continue;
        uint32_t* pt = (uint32_t*)(uintptr_t)(pd[i] & PAGE_ADDR_MASK);
        free_page((void*)pt);
    }
    free_page((void*)pd);
}

static int mark_user_page(uint32_t* pd, uint32_t vaddr) {
    uint32_t pde_index = vaddr >> 22;
    if (pde_index >= mapped_pde_count) return -1;

    uint32_t pde = pd[pde_index];
    if ((pde & PAGE_FLAG_PRESENT) == 0) return -1;

    uint32_t* pt = (uint32_t*)(uintptr_t)(pde & PAGE_ADDR_MASK);
    uint32_t pte_index = (vaddr >> 12) & 0x3FFu;
    if ((pt[pte_index] & PAGE_FLAG_PRESENT) == 0) return -1;

    pt[pte_index] |= PAGE_FLAG_USER;
    pd[pde_index] |= PAGE_FLAG_USER;
    return 0;
}

int paging_mark_user_range(unsigned int page_directory, unsigned int start, unsigned int size) {
    if (page_directory == 0u || size == 0u) return -1;

    uint32_t* pd = (uint32_t*)(uintptr_t)page_directory;
    uint32_t page_start = align_down_u32(start, PAGE_SIZE);
    uint32_t page_end = align_up_u32(start + size, PAGE_SIZE);
    if (page_end < page_start) return -1;

    for (uint32_t addr = page_start; addr < page_end; addr += PAGE_SIZE) {
        if (mark_user_page(pd, addr) != 0) return -1;
    }
    return 0;
}

int paging_get_mapping(unsigned int page_directory,
                       unsigned int vaddr,
                       unsigned int* out_phys,
                       unsigned int* out_flags) {
    if (!page_directory || !out_phys || !out_flags) return -1;

    uint32_t* pd = (uint32_t*)(uintptr_t)page_directory;
    uint32_t pde_index = vaddr >> 22;
    if (pde_index >= mapped_pde_count) return -1;

    uint32_t pde = pd[pde_index];
    if ((pde & PAGE_FLAG_PRESENT) == 0) return -1;

    uint32_t* pt = (uint32_t*)(uintptr_t)(pde & PAGE_ADDR_MASK);
    uint32_t pte_index = (vaddr >> 12) & 0x3FFu;
    uint32_t pte = pt[pte_index];
    if ((pte & PAGE_FLAG_PRESENT) == 0) return -1;

    *out_phys = (pte & PAGE_ADDR_MASK) | (vaddr & 0xFFFu);
    *out_flags = pte & 0xFFFu;
    return 0;
}

int paging_map_page(unsigned int page_directory,
                    unsigned int vaddr,
                    unsigned int phys,
                    unsigned int flags) {
    if (!page_directory) return -1;

    uint32_t* pd = (uint32_t*)(uintptr_t)page_directory;
    uint32_t pde_index = vaddr >> 22;
    if (pde_index >= mapped_pde_count) return -1;

    uint32_t pde = pd[pde_index];
    if ((pde & PAGE_FLAG_PRESENT) == 0) return -1;

    uint32_t* pt = (uint32_t*)(uintptr_t)(pde & PAGE_ADDR_MASK);
    uint32_t pte_index = (vaddr >> 12) & 0x3FFu;
    pt[pte_index] = (phys & PAGE_ADDR_MASK) | (flags & 0xFFFu) | PAGE_FLAG_PRESENT;

    if ((paging_get_kernel_directory() == page_directory) ||
        (current_process >= 0 && current_process < MAX_PROCESSES &&
         process_table[current_process].page_directory == page_directory)) {
        asm volatile("invlpg (%0)" : : "r"((void*)(uintptr_t)(vaddr & PAGE_ADDR_MASK)) : "memory");
    }
    return 0;
}

int paging_set_page_writable(unsigned int page_directory, unsigned int vaddr, int writable) {
    unsigned int phys = 0;
    unsigned int flags = 0;
    if (paging_get_mapping(page_directory, vaddr, &phys, &flags) != 0) return -1;
    if (writable) flags |= PAGE_FLAG_RW;
    else flags &= ~PAGE_FLAG_RW;
    return paging_map_page(page_directory, vaddr, phys, flags);
}

// Public API: allocate one 4KiB physical page, returned as a kernel virtual
// address (identity-mapped).
void* alloc_page(void) {
    uint32_t rel = 0;
    if (buddy_alloc_block(0, &rel) != 0) return 0;

    uint32_t frame = buddy_base_frame + rel;
    bitmap_mark_range(frame, 1u, 1);
    frame_refcount[frame] = 1u;
    uint32_t phys = frame * PAGE_SIZE;
    zero_page_u32((uint32_t*)(uintptr_t)phys);
    return (void*)phys;
}

void* alloc_pages(unsigned int order) {
    uint32_t ord = order;
    uint32_t rel = 0;
    if (buddy_alloc_block(ord, &rel) != 0) return 0;

    uint32_t frame = buddy_base_frame + rel;
    uint32_t count = 1u << ord;
    bitmap_mark_range(frame, count, 1);
    for (uint32_t i = 0; i < count; i++) frame_refcount[frame + i] = 1u;
    return (void*)(uintptr_t)(frame * PAGE_SIZE);
}

// Public API: free a previously allocated 4KiB page.
void free_page(void* addr) {
    free_pages(addr, 0u);
}

void free_pages(void* addr, unsigned int order) {
    uint32_t phys = (uint32_t)addr;
    if (phys < FIRST_MANAGED_PHYS || phys >= tracked_memory_top) {
        return;
    }
    if ((phys & (PAGE_SIZE - 1u)) != 0u) return;

    uint32_t frame = phys / PAGE_SIZE;
    if (frame < buddy_base_frame) return;

    uint32_t rel = frame - buddy_base_frame;
    uint32_t count = 1u << order;
    if (rel + count > buddy_total_frames) return;

    if (order == 0u && frame_refcount[frame] > 1u) {
        frame_refcount[frame]--;
        return;
    }

    bitmap_mark_range(frame, count, 0);
    for (uint32_t i = 0; i < count; i++) frame_refcount[frame + i] = 0u;
    buddy_free_block(rel, order);
}

int memory_smoke_test(void) {
    unsigned char* first_page = (unsigned char*)alloc_page();
    unsigned char* second_page = 0;

    if (!first_page) return -1;

    for (int i = 0; i < 64; i++) {
        if (first_page[i] != 0) {
            free_page(first_page);
            return -2;
        }
    }

    for (int i = 0; i < 64; i++) {
        first_page[i] = 0xA5;
    }

    free_page(first_page);

    second_page = (unsigned char*)alloc_page();
    if (!second_page) return -3;

    for (int i = 0; i < 64; i++) {
        if (second_page[i] != 0) {
            free_page(second_page);
            return -4;
        }
    }

    free_page(second_page);
    return 1;
}

void* kmalloc(unsigned int size) {
    if (size == 0u) return 0;

    uint32_t class_idx = SLAB_CLASS_COUNT;
    for (uint32_t i = 0; i < SLAB_CLASS_COUNT; i++) {
        if (size <= slab_classes[i].obj_size) {
            class_idx = i;
            break;
        }
    }

    if (class_idx == SLAB_CLASS_COUNT) {
        if (size > PAGE_SIZE) return 0;
        return alloc_page();
    }

    uint32_t page_addr = slab_classes[class_idx].head;
    SlabPage* slab = 0;
    while (page_addr) {
        slab = (SlabPage*)(uintptr_t)page_addr;
        if (slab->magic == SLAB_MAGIC && slab->free_count > 0) break;
        page_addr = slab->next;
        slab = 0;
    }

    if (!slab) {
        if (slab_refill_class(class_idx) != 0) return 0;
        slab = (SlabPage*)(uintptr_t)slab_classes[class_idx].head;
    }

    if (slab->free_head == SLAB_FREE_NONE) return 0;
    uint16_t idx = slab->free_head;
    uint32_t data_off = slab_page_data_offset();
    uint8_t* base = (uint8_t*)slab + data_off;
    uint8_t* obj = base + (uint32_t)idx * slab->obj_size;
    uint16_t* next_idx = (uint16_t*)obj;
    slab->free_head = *next_idx;
    slab->free_count--;
    return (void*)obj;
}

void kfree(void* ptr) {
    if (!ptr) return;

    uint32_t addr = (uint32_t)(uintptr_t)ptr;
    uint32_t page_base = align_down_u32(addr, PAGE_SIZE);
    SlabPage* slab = (SlabPage*)(uintptr_t)page_base;

    if (slab->magic == SLAB_MAGIC) {
        uint32_t data_off = slab_page_data_offset();
        uint32_t data_start = page_base + data_off;
        uint32_t data_end = page_base + PAGE_SIZE;
        if (addr >= data_start && addr < data_end && slab->obj_size != 0) {
            uint32_t rel = addr - data_start;
            if ((rel % slab->obj_size) != 0) return;
            uint16_t idx = (uint16_t)(rel / slab->obj_size);
            if (idx >= slab->capacity) return;

            uint16_t* slot = (uint16_t*)(uintptr_t)addr;
            *slot = slab->free_head;
            slab->free_head = idx;
            if (slab->free_count < slab->capacity) slab->free_count++;
            return;
        }
    }

    if ((addr & (PAGE_SIZE - 1u)) == 0u) {
        free_page(ptr);
    }
}

unsigned int paging_get_kernel_directory(void) {
    return (unsigned int)(uintptr_t)kernel_page_directory;
}

unsigned int paging_create_process_directory(unsigned int user_code_addr,
                                             unsigned int user_stack_base,
                                             unsigned int user_stack_size) {
    if (!kernel_page_directory || mapped_pde_count == 0) {
        return 0;
    }

    uint32_t* pd = alloc_zeroed_page();
    if (!pd) return 0;

    for (uint32_t i = 0; i < mapped_pde_count; i++) {
        if ((kernel_page_directory[i] & PAGE_FLAG_PRESENT) == 0) continue;

        uint32_t* src_pt = (uint32_t*)(uintptr_t)(kernel_page_directory[i] & PAGE_ADDR_MASK);
        uint32_t* dst_pt = alloc_zeroed_page();
        if (!dst_pt) {
            destroy_process_directory_internal(pd);
            return 0;
        }

        for (int j = 0; j < 1024; j++) {
            dst_pt[j] = src_pt[j] & ~PAGE_FLAG_USER;
        }

        pd[i] = ((unsigned int)(uintptr_t)dst_pt) | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    }

    if (user_code_addr != 0u && mark_user_page(pd, user_code_addr) != 0) {
        destroy_process_directory_internal(pd);
        return 0;
    }

    if (user_stack_base != 0u && user_stack_size != 0u) {
        uint32_t start = user_stack_base & ~(PAGE_SIZE - 1u);
        uint32_t end = align_up_u32(user_stack_base + user_stack_size, PAGE_SIZE);
        for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
            if (mark_user_page(pd, addr) != 0) {
                destroy_process_directory_internal(pd);
                return 0;
            }
        }
    }

    return (unsigned int)(uintptr_t)pd;
}

void paging_destroy_process_directory(unsigned int page_directory) {
    uint32_t* pd = (uint32_t*)(uintptr_t)page_directory;
    if (!pd) return;
    if (pd == kernel_page_directory) return;
    destroy_process_directory_internal(pd);
}

void paging_switch_directory(unsigned int page_directory) {
    if (page_directory == 0u) return;
    asm volatile("mov %0, %%cr3" : : "r"(page_directory) : "memory");
}

// Initialize paging with identity-mapped 4 KiB pages up to tracked_memory_top.
void init_paging(uint32_t mb_magic, uint32_t mb_info_addr) {
    init_frame_allocator(mb_magic, mb_info_addr);
    init_buddy_allocator();
    slab_init();

    kernel_page_directory = alloc_zeroed_page();
    if (!kernel_page_directory) {
        while (1) { }
    }

    mapped_pde_count = (tracked_memory_top + PAGE_4M_SIZE - 1u) / PAGE_4M_SIZE;
    if (mapped_pde_count > 1024u) mapped_pde_count = 1024u;

    for (uint32_t i = 0; i < mapped_pde_count; i++) {
        uint32_t* page_table = alloc_zeroed_page();
        if (!page_table) {
            while (1) { }
        }

        uint32_t base = i * PAGE_4M_SIZE;
        for (uint32_t j = 0; j < 1024; j++) {
            uint32_t phys = base + (j * PAGE_SIZE);
            if (phys >= tracked_memory_top) break;
            // Present | RW (supervisor-only)
            page_table[j] = phys | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
        }

        // Present | RW (supervisor-only)
        kernel_page_directory[i] = ((unsigned int)(uintptr_t)page_table) | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    }

    // Ensure classic 4 KiB paging mode.
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~0x10u;
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    uint32_t pd_phys = (uint32_t)(uintptr_t)kernel_page_directory;
    asm volatile("mov %0, %%cr3" : : "r"(pd_phys));

    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

