#include "kernel.h"

#define ELF_MAGIC 0x464C457Fu
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define EM_386 3u
#define PT_LOAD 1u

#define PF_X 0x1u
#define PF_W 0x2u
#define PF_R 0x4u
#define ELF_MAX_IMAGE_SIZE (4u * 1024u * 1024u)
#define PAGE_SIZE 4096u
#define ELF_MIN_USER_ADDRESS (1u * 1024u * 1024u)
#define ELF_MAX_USER_ADDRESS (16u * 1024u * 1024u)

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

static void elf_memcpy(void* dst, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
}

static void elf_memset(void* dst, unsigned char val, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    for (unsigned int i = 0; i < n; i++) d[i] = val;
}

static int read_entire_file(const char* path, unsigned char* out, int max_len, int* out_len) {
    int fd = vfs_open(path, FS_O_READ);
    if (fd < 0) return -1;

    int total = 0;
    while (total < max_len) {
        int n = vfs_read(fd, (char*)out + total, max_len - total);
        if (n < 0) {
            vfs_close(fd);
            return -1;
        }
        if (n == 0) break;
        total += n;
    }

    vfs_close(fd);
    if (out_len) *out_len = total;
    return 0;
}

int elf_load(const char* path, PCB* proc) {
    if (!path || !proc) return -1;

    unsigned int image_order = 10u; // 4 MiB maximum image
    unsigned char* elf_image = (unsigned char*)alloc_pages(image_order);
    if (!elf_image) return -2;

    int elf_len = 0;
    if (read_entire_file(path, elf_image, (int)ELF_MAX_IMAGE_SIZE, &elf_len) != 0) {
        free_pages(elf_image, image_order);
        return -2;
    }

    if (elf_len < (int)sizeof(Elf32_Ehdr)) {
        free_pages(elf_image, image_order);
        return -3;
    }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)elf_image;

    if (*(const uint32_t*)eh->e_ident != ELF_MAGIC) {
        free_pages(elf_image, image_order);
        return -4;
    }
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB) {
        free_pages(elf_image, image_order);
        return -5;
    }
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386) {
        free_pages(elf_image, image_order);
        return -6;
    }

    if (eh->e_ehsize < sizeof(Elf32_Ehdr) ||
        eh->e_phentsize < sizeof(Elf32_Phdr) ||
        eh->e_phnum == 0u) {
        free_pages(elf_image, image_order);
        return -7;
    }
    if ((uint32_t)eh->e_phnum > 16u ||
        (uint32_t)eh->e_phentsize > 256u ||
        eh->e_phoff > (uint32_t)elf_len ||
        (uint32_t)eh->e_phnum * (uint32_t)eh->e_phentsize >
            (uint32_t)elf_len - eh->e_phoff) {
        free_pages(elf_image, image_order);
        return -7;
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr* ph = (const Elf32_Phdr*)(elf_image +
            eh->e_phoff + (uint32_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_memsz == 0u) continue;
        if (ph->p_filesz > ph->p_memsz) goto elf_fail;
        if (ph->p_offset > (uint32_t)elf_len ||
            ph->p_filesz > (uint32_t)elf_len - ph->p_offset) goto elf_fail;
        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) goto elf_fail;
        if (ph->p_vaddr < ELF_MIN_USER_ADDRESS) goto elf_fail;
        if (ph->p_vaddr >= ELF_MAX_USER_ADDRESS) goto elf_fail;
        if (ph->p_memsz > ELF_MAX_USER_ADDRESS - ph->p_vaddr) goto elf_fail;

        uint32_t page_start = ph->p_vaddr & ~(PAGE_SIZE - 1u);
        uint32_t page_end = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1u) &
                            ~(PAGE_SIZE - 1u);
        if (page_end < page_start) goto elf_fail;
        unsigned int page_count = (page_end - page_start) / PAGE_SIZE;

        int region_slot = -1;
        for (int r = 0; r < MAX_PROCESS_MMAP_REGIONS; r++) {
            if (!proc->elf_regions[r].in_use) {
                region_slot = r;
                break;
            }
        }
        if (region_slot < 0) goto elf_fail;
        proc->elf_regions[region_slot].in_use = 1;
        proc->elf_regions[region_slot].addr = page_start;
        proc->elf_regions[region_slot].size = page_count * PAGE_SIZE;

        unsigned int flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_FLAG_RW;

        for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
            unsigned char* page = (unsigned char*)alloc_page();
            if (!page) goto elf_fail;
            elf_memset(page, 0, PAGE_SIZE);

            uint32_t segment_start = ph->p_vaddr;
            uint32_t segment_end = ph->p_vaddr + ph->p_memsz;
            uint32_t file_end = ph->p_vaddr + ph->p_filesz;
            uint32_t copy_start = vaddr > segment_start ? vaddr : segment_start;
            uint32_t copy_end = vaddr + PAGE_SIZE;
            if (copy_end > file_end) copy_end = file_end;
            if (copy_end > segment_end) copy_end = segment_end;

            if (copy_end > copy_start) {
                uint32_t file_offset = ph->p_offset + (copy_start - ph->p_vaddr);
                uint32_t copy_len = copy_end - copy_start;
                if (file_offset <= (uint32_t)elf_len &&
                    copy_len <= (uint32_t)elf_len - file_offset) {
                    elf_memcpy(page + (copy_start - vaddr),
                               elf_image + file_offset,
                               copy_len);
                }
            }

            if (paging_map_page(proc->page_directory, vaddr,
                                (unsigned int)(uintptr_t)page, flags) != 0) {
                free_page(page);
                goto elf_fail;
            }
        }
    }

    int entry_mapped = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr* ph = (const Elf32_Phdr*)(elf_image +
            eh->e_phoff + (uint32_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0u) continue;
        if (eh->e_entry >= ph->p_vaddr &&
            eh->e_entry < ph->p_vaddr + ph->p_memsz) {
            entry_mapped = 1;
            break;
        }
    }
    if (!entry_mapped) goto elf_fail;

    proc->eip = eh->e_entry;
    free_pages(elf_image, image_order);
    return 0;

elf_fail:
    for (int r = 0; r < MAX_PROCESS_MMAP_REGIONS; r++) {
        if (!proc->elf_regions[r].in_use) continue;
        unsigned int pages = proc->elf_regions[r].size / PAGE_SIZE;
        for (unsigned int p = 0; p < pages; p++) {
            unsigned int vaddr = proc->elf_regions[r].addr + p * PAGE_SIZE;
            unsigned int phys = 0;
            unsigned int flags = 0;
            if (proc->page_directory &&
                paging_get_mapping(proc->page_directory, vaddr, &phys, &flags) == 0) {
                free_page((void*)(phys & 0xFFFFF000u));
            }
        }
        proc->elf_regions[r].in_use = 0;
        proc->elf_regions[r].addr = 0;
        proc->elf_regions[r].size = 0;
    }
    free_pages(elf_image, image_order);
    return -12;
}
