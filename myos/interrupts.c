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

static volatile int mouse_col = 40;
static volatile int mouse_row = 12;
static volatile int mouse_dirty = 0;
static volatile int mouse_wheel_delta = 0;
static volatile int mouse_packet_size = 3;
static volatile int mouse_cycle = 0;
static volatile int mouse_accum_x = 0;
static volatile int mouse_accum_y = 0;
static volatile int mouse_scroll_freeze_packets = 0;
static volatile unsigned char mouse_packet[4];

#define MOUSE_X_DIVISOR 6
#define MOUSE_Y_DIVISOR 12
#define MOUSE_SCROLL_FREEZE_PACKETS 3
#define MOUSE_MAX_STEP_PER_PACKET 1

static int clamp_step(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static int ps2_wait_write_ready(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) return 1;
    }
    return 0;
}

static int ps2_wait_read_ready(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) return 1;
    }
    return 0;
}

static void ps2_drain_output_buffer(void) {
    for (int i = 0; i < 32; i++) {
        if ((inb(0x64) & 0x01) == 0) break;
        (void)inb(0x60);
    }
}

static int mouse_write_device(unsigned char value) {
    if (!ps2_wait_write_ready()) return 0;
    outb(0x64, 0xD4);
    if (!ps2_wait_write_ready()) return 0;
    outb(0x60, value);
    return 1;
}

static int mouse_read_data(unsigned char* value) {
    if (!value) return 0;
    if (!ps2_wait_read_ready()) return 0;
    *value = inb(0x60);
    return 1;
}

static int mouse_send_command(unsigned char value) {
    unsigned char ack = 0;
    if (!mouse_write_device(value)) return 0;
    if (!mouse_read_data(&ack)) return 0;
    return ack == 0xFA;
}

static void mouse_set_sample_rate(unsigned char rate) {
    if (!mouse_send_command(0xF3)) return;
    mouse_send_command(rate);
}

static void mouse_handle_data_byte(unsigned char data) {
    if (mouse_cycle == 0 && (data & 0x08) == 0) {
        return;
    }

    mouse_packet[mouse_cycle++] = data;

    if (mouse_cycle >= mouse_packet_size) {
        int wheel = 0;
        int dx = (int)((signed char)mouse_packet[1]);
        int dy = (int)((signed char)mouse_packet[2]);

        if (mouse_packet_size == 4) {
            wheel = (int)((signed char)mouse_packet[3]);
        }

        if (wheel != 0) {
            mouse_scroll_freeze_packets = MOUSE_SCROLL_FREEZE_PACKETS;
            mouse_accum_x = 0;
            mouse_accum_y = 0;
        }

        if (wheel == 0 && mouse_scroll_freeze_packets > 0) {
            mouse_scroll_freeze_packets--;
            dx = 0;
            dy = 0;
        }

        if (wheel == 0) {
            mouse_accum_x += dx;
            mouse_accum_y += dy;

            int move_cols = mouse_accum_x / MOUSE_X_DIVISOR;
            int move_rows = mouse_accum_y / MOUSE_Y_DIVISOR;

            move_cols = clamp_step(move_cols, -MOUSE_MAX_STEP_PER_PACKET, MOUSE_MAX_STEP_PER_PACKET);
            move_rows = clamp_step(move_rows, -MOUSE_MAX_STEP_PER_PACKET, MOUSE_MAX_STEP_PER_PACKET);

            mouse_accum_x -= move_cols * MOUSE_X_DIVISOR;
            mouse_accum_y -= move_rows * MOUSE_Y_DIVISOR;

            if (move_cols != 0) {
                mouse_col += move_cols;
                if (mouse_col < 0) mouse_col = 0;
                if (mouse_col > 79) mouse_col = 79;
                mouse_dirty = 1;
            }

            if (move_rows != 0) {
                mouse_row -= move_rows;
                if (mouse_row < 0) mouse_row = 0;
                if (mouse_row > 24) mouse_row = 24;
                mouse_dirty = 1;
            }
        }

        if (wheel != 0) {
            mouse_wheel_delta += wheel;
            mouse_dirty = 1;
        }

        mouse_cycle = 0;
    }
}

// --- Interrupt Functions ---

void set_idt_entry(int n, unsigned int handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].type_attr = 0x8E;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void set_idt_entry_user(int n, unsigned int handler) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].type_attr = 0xEF;
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
    process_run_current_tick();
    process_maintenance_tick();
    if ((ticks % 6) == 0) {
        schedule();
    }
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

void mouse_handler() {
    unsigned char data = inb(0x60);
    mouse_handle_data_byte(data);

    asm volatile("outb %0, %1" : : "a"((unsigned char)PIC_EOI), "Nd"((uint16_t)PIC2_COMMAND));
    asm volatile("outb %0, %1" : : "a"((unsigned char)PIC_EOI), "Nd"((uint16_t)PIC1_COMMAND));
}

int keyboard_pop_scancode(unsigned char* out_scancode) {
    if (!out_scancode) return 0;
    if (keyboard_tail == keyboard_head) return 0;

    *out_scancode = keyboard_buffer[keyboard_tail];
    keyboard_tail = (keyboard_tail + 1) & 0xFF;
    return 1;
}

void mouse_init(void) {
    unsigned char status = 0;
    unsigned char device_id = 0;

    ps2_drain_output_buffer();

    if (!ps2_wait_write_ready()) return;
    outb(0x64, 0xA8);

    if (!ps2_wait_write_ready()) return;
    outb(0x64, 0x20);
    if (!mouse_read_data(&status)) return;

    status |= 0x02;
    status &= (unsigned char)~0x20;

    if (!ps2_wait_write_ready()) return;
    outb(0x64, 0x60);
    if (!ps2_wait_write_ready()) return;
    outb(0x60, status);

    ps2_drain_output_buffer();

    mouse_send_command(0xF6);
    mouse_set_sample_rate(200);
    mouse_set_sample_rate(100);
    mouse_set_sample_rate(80);

    if (mouse_send_command(0xF2) && mouse_read_data(&device_id)) {
        if (device_id == 0x03 || device_id == 0x04) {
            mouse_packet_size = 4;
        }
    }

    mouse_send_command(0xF4);
    ps2_drain_output_buffer();
    mouse_dirty = 1;
}

void mouse_poll_hardware(void) {
    for (int i = 0; i < 16; i++) {
        unsigned char status = inb(0x64);
        if ((status & 0x01) == 0) break;
        if ((status & 0x20) == 0) break;
        mouse_handle_data_byte(inb(0x60));
    }
}

int mouse_poll_state(MouseState* state) {
    if (!state) return 0;
    if (!mouse_dirty && mouse_wheel_delta == 0) return 0;

    state->col = mouse_col;
    state->row = mouse_row;
    state->wheel_delta = mouse_wheel_delta;

    mouse_wheel_delta = 0;
    mouse_dirty = 0;
    return 1;
}