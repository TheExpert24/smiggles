#include "kernel.h"

// Page size used by the paging code
#define PAGE_SIZE 4096u

// Helper: round down to page
static inline unsigned int page_round_down(unsigned int v) {
    return v & ~(PAGE_SIZE - 1u);
}
static inline unsigned int page_round_up(unsigned int v) {
    return (v + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

int validate_user_ptr(const void* ptr, unsigned int len, int write_ok) {
    if (len == 0) return 1; // empty range is valid
    if (!ptr) return 0;

    unsigned int start = (unsigned int)(uintptr_t)ptr;
    unsigned int end = start + len;
    if (end < start) return 0; // overflow

    if (current_process < 0 || current_process >= MAX_PROCESSES) return 0;
    unsigned int pd = process_table[current_process].page_directory;
    if (pd == 0u) return 0;

    unsigned int pstart = page_round_down(start);
    unsigned int pend = page_round_up(end);

    for (unsigned int v = pstart; v < pend; v += PAGE_SIZE) {
        unsigned int phys = 0, flags = 0;
        if (paging_get_mapping(pd, v, &phys, &flags) != 0) {
            return 0;
        }
        // Must be user-accessible
        if ((flags & PAGE_FLAG_USER) == 0u) return 0;
        if (write_ok) {
            if ((flags & PAGE_FLAG_RW) == 0u) return 0;
        }
    }

    return 1;
}

int copy_from_user(void* dst, const void* user_src, unsigned int len) {
    if (!dst) return LINUX_EFAULT;
    if (!validate_user_ptr(user_src, len, 0)) return LINUX_EFAULT;

    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)user_src;
    for (unsigned int i = 0; i < len; i++) d[i] = s[i];
    return 0;
}

int copy_to_user(void* user_dst, const void* src, unsigned int len) {
    if (!src) return LINUX_EFAULT;
    if (!validate_user_ptr(user_dst, len, 1)) return LINUX_EFAULT;

    unsigned char* d = (unsigned char*)user_dst;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < len; i++) d[i] = s[i];
    return 0;
}
