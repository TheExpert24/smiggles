#include "kernel.h"

// --- Global Variables ---
struct IDT_entry idt[256];
struct IDT_ptr idt_ptr;

volatile int ticks = 0;
char last_key = 0;
int just_saved = 0;
int skip_next_prompt = 0;

static volatile unsigned char keyboard_buffer[256];
static volatile unsigned int keyboard_head = 0;
static volatile unsigned int keyboard_tail = 0;

// --- Interrupt Functions ---

void set_idt_entry(int n, unsigned int handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].type_attr = 0x8E;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void pic_remap() {
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x11), "Nd"((uint16_t)PIC1_COMMAND));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x11), "Nd"((uint16_t)PIC2_COMMAND));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x20), "Nd"((uint16_t)PIC1_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x28), "Nd"((uint16_t)PIC2_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x04), "Nd"((uint16_t)PIC1_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x02), "Nd"((uint16_t)PIC2_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x01), "Nd"((uint16_t)PIC1_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0x01), "Nd"((uint16_t)PIC2_DATA));
}

// C handlers called from ASM stubs
void timer_handler() {
    ticks++;
    asm volatile("outb %0, %1" : : "a"((unsigned char)PIC_EOI), "Nd"((uint16_t)PIC1_COMMAND));
}

void keyboard_handler() {
    unsigned char scancode;
    asm volatile("inb $0x60, %0" : "=a"(scancode));
    last_key = (char)scancode;

    unsigned int next_head = (keyboard_head + 1) & 0xFF;
    if (next_head != keyboard_tail) {
        keyboard_buffer[keyboard_head] = scancode;
        keyboard_head = next_head;
    }

    asm volatile("outb %0, %1" : : "a"((unsigned char)PIC_EOI), "Nd"((uint16_t)PIC1_COMMAND));
}

int keyboard_pop_scancode(unsigned char* out_scancode) {
    if (!out_scancode) return 0;
    if (keyboard_tail == keyboard_head) return 0;

    *out_scancode = keyboard_buffer[keyboard_tail];
    keyboard_tail = (keyboard_tail + 1) & 0xFF;
    return 1;
}