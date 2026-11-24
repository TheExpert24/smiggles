#ifndef SMIGGLES_INTERRUPTS_H
#define SMIGGLES_INTERRUPTS_H

void pic_remap(void);
void set_idt_entry(int n, unsigned int handler);
void set_idt_entry_user(int n, unsigned int handler);
void init_protection(void);
int protection_is_ready(void);
unsigned int protection_get_cpl(void);
void protection_set_kernel_stack(unsigned int kernel_esp0);
int keyboard_pop_scancode(unsigned char* out_scancode);
int keyboard_available(void);
void mouse_init(void);
void mouse_poll_hardware(void);

#endif
