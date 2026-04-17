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

    static unsigned char elf_image[64 * 1024];
    int elf_len = 0;
    if (read_entire_file(path, elf_image, (int)sizeof(elf_image), &elf_len) != 0) {
        return -2;
    }

    if (elf_len < (int)sizeof(Elf32_Ehdr)) return -3;
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)elf_image;

    if (*(const uint32_t*)eh->e_ident != ELF_MAGIC) return -4;
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB) return -5;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386) return -6;

    if (eh->e_phoff + (uint32_t)eh->e_phnum * (uint32_t)eh->e_phentsize > (uint32_t)elf_len) return -7;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr* ph = (const Elf32_Phdr*)(elf_image + eh->e_phoff + (uint32_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;

        if (ph->p_memsz == 0u) continue;
        if (ph->p_filesz > ph->p_memsz) return -8;
        if (ph->p_offset + ph->p_filesz > (uint32_t)elf_len) return -9;

        if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) return -10;

        // This kernel currently uses identity mapping in low memory.
        // Reject high virtual addresses that are not mapped.
        if (ph->p_vaddr >= (16u * 1024u * 1024u)) return -11;

        elf_memset((void*)(uintptr_t)ph->p_vaddr, 0, ph->p_memsz);
        if (ph->p_filesz > 0u) {
            elf_memcpy((void*)(uintptr_t)ph->p_vaddr, elf_image + ph->p_offset, ph->p_filesz);
        }

        if (paging_mark_user_range(proc->page_directory, ph->p_vaddr, ph->p_memsz) != 0) {
            return -12;
        }

        (void)ph->p_flags;
    }

    proc->eip = eh->e_entry;
    if (paging_mark_user_range(proc->page_directory, eh->e_entry, 4096u) != 0) {
        return -13;
    }
    return 0;
}
