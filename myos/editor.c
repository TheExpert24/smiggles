#include "kernel.h"

// --- Global Variables ---
RamFile file_table[MAX_FILES];
int file_count = 0;

// --- Nano-like Text Editor ---
void nano_editor(const char* filename, char* video, int* cursor) {
    int node_idx = resolve_path(filename);
    if (node_idx == -1 || node_table[node_idx].type != NODE_FILE) {
        // Create file if it doesn't exist
        node_idx = fs_touch(filename, "");
        if (node_idx < 0) {
            print_string("Cannot create file", 18, video, cursor, 0xC);
            return;
        }
    }
    
    char* buf = node_table[node_idx].content;
    // Save the current screen and cursor
    char prev_screen[80*25*2];
    for (int i = 0; i < 80*25*2; ++i) prev_screen[i] = video[i];
    int prev_cursor = *cursor;
    int pos = node_table[node_idx].content_size;
    int editing = 1;
    int maxlen = MAX_FILE_CONTENT - 1;
    
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    int header_cursor = 0;
    print_string("--- Nano Editor ---", -1, video, &header_cursor, 0x0B);
    print_string("Ctrl+S: Save | Ctrl+Q: Quit", -1, video, &header_cursor, 0x0F);
    int edit_start = 240;
    int logical_row = 0, logical_col = 0;
    int draw_cursor = edit_start;
    int cursor_row = 0, cursor_col = 0;
    for (int i = 0; i < pos && draw_cursor < 80*25; i++) {
        if (buf[i] == '\n') {
            logical_row++;
            logical_col = 0;
            draw_cursor = edit_start + logical_row * 80;
        } else {
            video[(draw_cursor)*2] = buf[i];
            video[(draw_cursor)*2+1] = 0x0F;
            draw_cursor++;
            logical_col++;
        }
        if (i == pos - 1) {
            cursor_row = logical_row;
            cursor_col = logical_col;
        }
    }
    for (; draw_cursor < edit_start + 80*22; draw_cursor++) {
        video[(draw_cursor)*2] = ' ';
        video[(draw_cursor)*2+1] = 0x07;
    }
    *cursor = edit_start + cursor_row * 80 + cursor_col;
    unsigned short pos_hw = *cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos_hw & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos_hw >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
    
    int shift = 0, ctrl = 0;
    unsigned char prev_scancode = 0;
    int exit_code = 0;
    
    while (editing) {
        unsigned char scancode;
        asm volatile("inb $0x60, %0" : "=a"(scancode));
        if (scancode == prev_scancode || scancode == 0) continue;
        prev_scancode = scancode;
        if (scancode & 0x80) {
            if (scancode == 0xAA || scancode == 0xB6) shift = 0;
            if (scancode == 0x9D) ctrl = 0;
            continue;
        }
        if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
        if (scancode == 0x1D) { ctrl = 1; continue; }
        if (ctrl && scancode == 0x1F) {
            node_table[node_idx].content_size = pos;
            buf[pos] = 0;
            while (1) {
                unsigned char sc;
                asm volatile("inb $0x60, %0" : "=a"(sc));
                if (sc == 0x9D) break;
            }
            exit_code = 1;
            break;
        }
        if (ctrl && scancode == 0x10) {
            node_table[node_idx].content_size = pos;
            buf[pos] = 0;
            while (1) {
                unsigned char sc;
                asm volatile("inb $0x60, %0" : "=a"(sc));
                if (sc == 0x9D) break;
            }
            exit_code = 2;
            break;
        }
        if (scancode == 0x1C && pos < maxlen) {
            buf[pos++] = '\n';
        }
        else if (scancode == 0x0E && pos > 0) {
            if (pos > 0) {
                pos--;
            }
        }
        else if (scancode < 128) {
            const char lower_table[128] = {
                [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
                [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
                [0x0C] = '-', [0x0D] = '=',
                [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
                [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
                [0x1A] = '[', [0x1B] = ']', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
                [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
                [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
                [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ',
            };
            const char upper_table[128] = {
                [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^',
                [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
                [0x0C] = '_', [0x0D] = '+',
                [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
                [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
                [0x1A] = '{', [0x1B] = '}', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
                [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
                [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
                [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ',
            };
            char c = shift ? upper_table[scancode] : lower_table[scancode];
            if (c && pos < maxlen) {
                buf[pos++] = c;
                video[(draw_cursor)*2] = c;
                video[(draw_cursor)*2+1] = 0x0F;
                draw_cursor++;
            }
        }
        int redraw_cursor = edit_start;
        for (int i = 0; i < pos && redraw_cursor < 80*25; i++) {
            if (buf[i] == '\n') {
                redraw_cursor = ((redraw_cursor / 80) + 1) * 80;
            } else {
                video[(redraw_cursor)*2] = buf[i];
                video[(redraw_cursor)*2+1] = 0x0F;
                redraw_cursor++;
            }
        }
        for (; redraw_cursor < 80*25; redraw_cursor++) {
            video[(redraw_cursor)*2] = ' ';
            video[(redraw_cursor)*2+1] = 0x07;
        }
        int cur_row = 0, cur_col = 0, temp = 0;
        for (int i = 0; i < pos; i++) {
            if (buf[i] == '\n') {
                cur_row++;
                cur_col = 0;
                temp = 0;
            } else {
                cur_col++;
                temp++;
            }
        }
        int cur_pos = edit_start + cur_row * 80 + cur_col;
        *cursor = cur_pos;
        unsigned short pos_hw = *cursor;
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos_hw & 0xFF)), "Nd"((unsigned short)0x3D5));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos_hw >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
    }
    
    // Restore previous screen
    for (int i = 0; i < 80*25*2; ++i) video[i] = prev_screen[i];
    *cursor = prev_cursor;
    
    // Move to end of the "edit" command line
    while (*cursor < 80*25 && video[(*cursor)*2] != 0 && video[(*cursor)*2] != ' ' && video[(*cursor)*2] != '\0') (*cursor)++;
    
    // Move to a new line for the status message
    *cursor = ((*cursor / 80) + 1) * 80;
    if (*cursor >= 80*25) {
        scroll_screen(video);
        *cursor -= 80;
    }
    
    // Print status message
    const char* msg = (exit_code == 1) ? "[Saved]" : "[Exited]";
    unsigned char msg_color = (exit_code == 1) ? 0x0A : 0x0C;
    int msg_len = 0;
    while (msg[msg_len]) msg_len++;
    for (int i = 0; i < msg_len && *cursor < 80*25 - 1; i++) {
        video[(*cursor)*2] = msg[i];
        video[(*cursor)*2+1] = msg_color;
        (*cursor)++;
    }
    
    // Drain keyboard buffer thoroughly
    volatile int drain_count = 0;
    while (drain_count < 100) {
        unsigned char dummy;
        asm volatile("inb $0x60, %0" : "=a"(dummy));
        drain_count++;
        for (volatile int d = 0; d < 1000; d++);
    }
}