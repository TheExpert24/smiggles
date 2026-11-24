#ifndef SMIGGLES_MEMORY_H
#define SMIGGLES_MEMORY_H

#include <stdint.h>

#define PAGE_SIZE_BYTES 4096u
#define PAGE_FLAG_PRESENT 0x001u
#define PAGE_FLAG_RW      0x002u
#define PAGE_FLAG_USER    0x004u
#define PAGE_FLAG_COW     0x200u

void init_paging(uint32_t mb_magic, uint32_t mb_info_addr);
void* alloc_page(void);
void* alloc_pages(unsigned int order);
void free_page(void* addr);
void free_pages(void* addr, unsigned int order);
int memory_smoke_test(void);
void* kmalloc(unsigned int size);
void kfree(void* ptr);
unsigned int paging_get_kernel_directory(void);
unsigned int paging_create_process_directory(unsigned int user_code_addr,
                                             unsigned int user_stack_base,
                                             unsigned int user_stack_size);
void paging_destroy_process_directory(unsigned int page_directory);
void paging_switch_directory(unsigned int page_directory);
int paging_mark_user_range(unsigned int page_directory, unsigned int start, unsigned int size);
int paging_get_mapping(unsigned int page_directory,
                       unsigned int vaddr,
                       unsigned int* out_phys,
                       unsigned int* out_flags);
int paging_map_page(unsigned int page_directory,
                    unsigned int vaddr,
                    unsigned int phys,
                    unsigned int flags);
int paging_set_page_writable(unsigned int page_directory, unsigned int vaddr, int writable);
void paging_inc_page_ref(unsigned int phys_addr);
unsigned int paging_get_page_ref(unsigned int phys_addr);
void paging_dec_page_ref(unsigned int phys_addr);

#endif
