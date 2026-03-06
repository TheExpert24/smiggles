#include "kernel.h"

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) GDTPtr;

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iopb;
} __attribute__((packed)) TSS32;

static uint64_t gdt[6];
static GDTPtr gdt_ptr;
static TSS32 tss;
static int protection_ready = 0;

static void set_gdt_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    uint64_t entry = 0;

    entry |= (uint64_t)(limit & 0xFFFF);
    entry |= (uint64_t)(base & 0xFFFFFF) << 16;
    entry |= (uint64_t)access << 40;
    entry |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    entry |= (uint64_t)(gran & 0xF0) << 48;
    entry |= (uint64_t)((base >> 24) & 0xFF) << 56;

    gdt[idx] = entry;
}

void init_protection(void) {
    for (int i = 0; i < 6; i++) gdt[i] = 0;

    // 0x08 Kernel code, 0x10 Kernel data
    set_gdt_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);
    set_gdt_entry(2, 0, 0xFFFFF, 0x92, 0xCF);

    // 0x18 User code, 0x20 User data
    set_gdt_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);
    set_gdt_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);

    // TSS descriptor at 0x28
    for (unsigned int i = 0; i < sizeof(TSS32); i++) {
        ((unsigned char*)&tss)[i] = 0;
    }
    tss.ss0 = 0x10;
    tss.esp0 = 0x90000;
    tss.iopb = sizeof(TSS32);
    set_gdt_entry(5, (uint32_t)&tss, sizeof(TSS32) - 1, 0x89, 0x00);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt[0];

    asm volatile("lgdt %0" : : "m"(gdt_ptr));

    asm volatile(
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        :
        :
        : "ax", "memory"
    );

    asm volatile(
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        :
        : "ax", "memory"
    );

    protection_ready = 1;
}

int protection_is_ready(void) {
    return protection_ready;
}

unsigned int protection_get_cpl(void) {
    unsigned int cs = 0;
    asm volatile("mov %%cs, %0" : "=r"(cs));
    return cs & 0x3;
}
