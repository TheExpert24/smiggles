// Basic paging and physical frame allocator for Smiggles OS
// Inspired by Linux's virtual memory concepts:
// - paged virtual memory over physical frames
// - simple physical page allocator for kernel use

#include "kernel.h"

#define PAGE_SIZE          4096
#define PHYS_MEMORY_LIMIT  (16 * 1024 * 1024)   // Manage first 16 MiB
#define FIRST_MANAGED_PHYS 0x00100000           // Start managing at 1 MiB

#define FIRST_MANAGED_FRAME   (FIRST_MANAGED_PHYS / PAGE_SIZE)
#define TOTAL_FRAMES          (PHYS_MEMORY_LIMIT / PAGE_SIZE)
#define FRAME_BITMAP_SIZE     (TOTAL_FRAMES / 32)

// Frame allocation bitmap: 1 = used, 0 = free
static uint32_t frame_bitmap[FRAME_BITMAP_SIZE];

// Page directory and a small set of page tables to identity-map low memory.
// 4 * 4MiB = 16MiB identity mapping.
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[4][1024] __attribute__((aligned(4096)));

static inline void set_frame(uint32_t frame) {
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    frame_bitmap[idx] |= (1U << bit);
}

static inline void clear_frame(uint32_t frame) {
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    frame_bitmap[idx] &= ~(1U << bit);
}

static inline int frame_is_set(uint32_t frame) {
    uint32_t idx = frame / 32;
    uint32_t bit = frame % 32;
    return (frame_bitmap[idx] & (1U << bit)) != 0;
}

// Find the first free physical frame at or above FIRST_MANAGED_FRAME.
static int first_free_frame(void) {
    for (uint32_t frame = FIRST_MANAGED_FRAME; frame < TOTAL_FRAMES; frame++) {
        if (!frame_is_set(frame)) {
            return (int)frame;
        }
    }
    return -1;
}

// Initialize the simple frame allocator.
static void init_frame_allocator(void) {
    for (int i = 0; i < FRAME_BITMAP_SIZE; i++) {
        frame_bitmap[i] = 0;
    }
    // Frames below FIRST_MANAGED_FRAME are never handed out (reserved for kernel/stack/BIOS).
    for (uint32_t f = 0; f < FIRST_MANAGED_FRAME; f++) {
        set_frame(f);
    }
}

// Public API: allocate one 4KiB physical page, returned as a kernel virtual
// address (identity-mapped).
void* alloc_page(void) {
    int frame = first_free_frame();
    if (frame < 0) {
        return 0; // Out of managed memory
    }
    set_frame((uint32_t)frame);
    uint32_t phys = (uint32_t)frame * PAGE_SIZE;
    return (void*)phys; // Identity mapping: virtual == physical
}

// Public API: free a previously allocated 4KiB page.
void free_page(void* addr) {
    uint32_t phys = (uint32_t)addr;
    if (phys < FIRST_MANAGED_PHYS || phys >= PHYS_MEMORY_LIMIT) {
        return; // Ignore addresses outside managed range
    }
    uint32_t frame = phys / PAGE_SIZE;
    clear_frame(frame);
}

// Initialize paging with a simple identity-mapped layout over the first 16 MiB.
// This provides a Linux-like paged virtual memory foundation.
void init_paging(void) {
    init_frame_allocator();

    // Clear page directory
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }

    // Identity-map first 16 MiB using 4 page tables.
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 1024; i++) {
            uint32_t addr = (uint32_t)((t * 1024 + i) * PAGE_SIZE);
            // Present (bit 0) | Read/Write (bit 1) | Supervisor (bit 2 = 0)
            page_tables[t][i] = addr | 0x3;
        }
        // Present | RW | Supervisor for page directory entries
        page_directory[t] = ((uint32_t)page_tables[t]) | 0x3;
    }

    // Load page directory base into CR3
    uint32_t pd_phys = (uint32_t)page_directory;
    asm volatile("mov %0, %%cr3" : : "r"(pd_phys));

    // Enable paging (set PG bit in CR0)
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

