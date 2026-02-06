#include "kernel.h"

// --- Global Variables ---
int line_start = 0;
int cmd_len = 0;
int cmd_cursor = 0;
int history_position = -1;  // -1 means not navigating history
int tab_completion_active = 0;
int tab_completion_position = -1;
int tab_match_count = 0;
char tab_matches[32][32];

void kernel_main(void) {
    // Initialize filesystem FIRST
    init_filesystem();
    
    // --- Interrupt setup ---
    pic_remap();
    set_idt_entry(0x20, (unsigned int)irq0_timer_handler);
    set_idt_entry(0x21, (unsigned int)irq1_keyboard_handler);
    extern struct IDT_ptr idt_ptr;
    extern struct IDT_entry idt[256];
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (unsigned int)&idt;
    load_idt(&idt_ptr);
    asm volatile("sti");
    char* video = (char*)0xB8000;
    int cursor = 0;
    int prompt_end = 0;
    int line_start = 0;
    unsigned char prev_scancode = 0;
    int shift = 0;

    
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }

    //introductory message
    print_smiggles_art(video, &cursor);
    cursor += 80; // add one line space
    const char* msg = "> ";
    int i = 0;
    while (msg[i]) {
        video[cursor*2] = msg[i];
        video[cursor*2+1] = 0x0F;
        cursor++;
        i++;
    }
    prompt_end = cursor;
    line_start = cursor;

    
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((uint16_t)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));

    
    char cmd_buf[64];
    // Use global cmd_len and cmd_cursor so nano_editor can reset them
    // int cmd_len = 0;
    // int cmd_cursor = 0; // position within the input line

    // Enable cursor
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0A), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0B), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)15), "Nd"((unsigned short)0x3D5));

    while (1) {
        unsigned char scancode;
        asm volatile("inb $0x60, %0" : "=a"(scancode));

        //SHIFT KEYS
        if (scancode == 0x2A || scancode == 0x36) { 
            shift = 1;
            continue;
        }
        if (scancode == 0xAA || scancode == 0xB6) { 
            shift = 0;
            continue;
        }

        // Handle E0 prefix for arrow keys
        int e0_prefix = 0;
        if (scancode == 0xE0) {
            e0_prefix = 1;
            // Get next scancode - wait for a valid make code
            unsigned char next_scancode = 0;
            while (1) {
                asm volatile("inb $0x60, %0" : "=a"(next_scancode));
                if (next_scancode != 0xE0 && next_scancode != 0 && next_scancode < 0x80) {
                    scancode = next_scancode;
                    break;
                }
            }
        }

        // Filter out release codes for non-E0 keys
        if (!e0_prefix && scancode > 0x80) {
            prev_scancode = 0;
            continue;
        }
        
        // For arrow keys, don't check prev_scancode to allow repeated presses
        if (!e0_prefix) {
            if (scancode == prev_scancode || scancode == 0) continue;
            prev_scancode = scancode;
        }

        if (e0_prefix) {
            if (scancode == 0x4B) { // Left arrow
                if (tab_completion_active && tab_completion_position > 0) {
                    // Navigate tab completion backwards
                    tab_completion_position--;
                    
                    // Clear current line
                    for (int i = 0; i < 63; i++) {
                        video[(line_start + i)*2] = ' ';
                        video[(line_start + i)*2+1] = 0x07;
                    }
                    
                    // Write selected completion
                    cmd_len = 0;
                    int j = 0;
                    while (tab_matches[tab_completion_position][j] && cmd_len < 63) {
                        cmd_buf[cmd_len] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2+1] = 0x0F;
                        cmd_len++;
                        j++;
                    }
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                } else if (cmd_cursor > 0) {
                    cmd_cursor--;
                    cursor--;
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    asm volatile("inb $0x60, %0" : "=a"(rel));
                    if (rel == 0xCB) break; // Release code for left arrow
                }
                continue;
            } else if (scancode == 0x4D) { // Right arrow
                if (tab_completion_active && tab_completion_position < tab_match_count - 1) {
                    // Navigate tab completion forwards
                    tab_completion_position++;
                    
                    // Clear current line
                    for (int i = 0; i < 63; i++) {
                        video[(line_start + i)*2] = ' ';
                        video[(line_start + i)*2+1] = 0x07;
                    }
                    
                    // Write selected completion
                    cmd_len = 0;
                    int j = 0;
                    while (tab_matches[tab_completion_position][j] && cmd_len < 63) {
                        cmd_buf[cmd_len] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2+1] = 0x0F;
                        cmd_len++;
                        j++;
                    }
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                } else if (cmd_cursor < cmd_len) {
                    cmd_cursor++;
                    cursor++;
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    asm volatile("inb $0x60, %0" : "=a"(rel));
                    if (rel == 0xCD) break; // Release code for right arrow
                }
                continue;
            } else if (scancode == 0x48) { // Up arrow
                if (history_count > 0) {
                    // Clear current line
                    for (int i = 0; i < cmd_len; i++) {
                        video[(line_start + i)*2] = ' ';
                        video[(line_start + i)*2+1] = 0x07;
                    }
                    
                    // Move to previous command in history
                    if (history_position == -1) {
                        history_position = history_count - 1;
                    } else if (history_position > 0) {
                        history_position--;
                    }
                    
                    // Load command from history
                    cmd_len = 0;
                    while (history[history_position][cmd_len] && cmd_len < 63) {
                        cmd_buf[cmd_len] = history[history_position][cmd_len];
                        video[(line_start + cmd_len)*2] = cmd_buf[cmd_len];
                        video[(line_start + cmd_len)*2+1] = 0x0F;
                        cmd_len++;
                    }
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    asm volatile("inb $0x60, %0" : "=a"(rel));
                    if (rel == 0xC8) break; // Release code for up arrow
                }
                continue;
            } else if (scancode == 0x50) { // Down arrow
                if (history_position != -1) {
                    // Clear current line
                    for (int i = 0; i < cmd_len; i++) {
                        video[(line_start + i)*2] = ' ';
                        video[(line_start + i)*2+1] = 0x07;
                    }
                    
                    // Move to next command in history
                    if (history_position < history_count - 1) {
                        history_position++;
                        
                        // Load command from history
                        cmd_len = 0;
                        while (history[history_position][cmd_len] && cmd_len < 63) {
                            cmd_buf[cmd_len] = history[history_position][cmd_len];
                            video[(line_start + cmd_len)*2] = cmd_buf[cmd_len];
                            video[(line_start + cmd_len)*2+1] = 0x0F;
                            cmd_len++;
                        }
                    } else {
                        // Return to empty line
                        history_position = -1;
                        cmd_len = 0;
                    }
                    
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    asm volatile("inb $0x60, %0" : "=a"(rel));
                    if (rel == 0xD0) break; // Release code for down arrow
                }
                continue;
            }
            continue; // Ignore other E0 keys
        }

        char c = 0;
        const char lower_table[128] = {
            [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
            [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
            [0x0C] = '-', [0x0D] = '=',
            [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
            [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
            [0x1A] = '[', [0x1B] = ']', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
            [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
            [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
            [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ', [0x1C] = '\n', [0x0E] = 8,
            [0x0F] = '\t',
            [0x37] = '*', [0x4A] = '-', [0x4E] = '+',
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
            [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ', [0x1C] = '\n', [0x0E] = 8,
            [0x0F] = '\t',
            [0x37] = '*', [0x4A] = '-', [0x4E] = '+',
        };

        if (shift)
            c = upper_table[scancode];
        else
            c = lower_table[scancode];

        if (c) {
            // Any key press deactivates tab completion mode
            if (c != '\t' && c != '\n') {
                tab_completion_active = 0;
                tab_completion_position = -1;
            }
            
            if (c == '\n') {
                tab_completion_active = 0;
                tab_completion_position = -1;
                cmd_buf[cmd_len] = 0;
                if (cmd_buf[0] == 'p' && cmd_buf[1] == 'r' && cmd_buf[2] == 'i' && cmd_buf[3] == 'n' && cmd_buf[4] == 't' && cmd_buf[5] == ' ' && cmd_buf[6] == '"') {
                    int start = 7;
                    int end = start;
                    while (cmd_buf[end] && cmd_buf[end] != '"') end++;
                    if (cmd_buf[end] == '"') {
                        print_string(&cmd_buf[start], end - start, video, &cursor, 0x0D);
                    }
                } else {
                    dispatch_command(cmd_buf, video, &cursor);
                }
                // New prompt
                cursor = ((cursor / 80) + 1) * 80;
                if (cursor >= 80*25) {
                    scroll_screen(video);
                    cursor -= 80;
                }
                const char* prompt = "> ";
                int pi = 0;
                while (prompt[pi] && cursor < 80*25 - 1) {
                    video[cursor*2] = prompt[pi];
                    video[cursor*2+1] = 0x0F;
                    cursor++;
                    pi++;
                }
                unsigned short pos_hw = cursor;
                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos_hw & 0xFF)), "Nd"((unsigned short)0x3D5));
                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos_hw >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                line_start = cursor;
                cmd_len = 0;
                cmd_cursor = 0;
                history_position = -1;  // Reset history navigation
            }
            else if (c == 8) {
                if (cmd_cursor > 0 && cmd_len > 0 && cursor > line_start) {
                    for (int k = cmd_cursor-1; k < cmd_len-1; k++)
                        cmd_buf[k] = cmd_buf[k+1];
                    cmd_len--;
                    cmd_cursor--;
                    cursor--;
                    int redraw = cursor;
                    for (int k = 0; k < cmd_len-cmd_cursor; k++) {
                        video[(redraw+k)*2] = cmd_buf[cmd_cursor+k];
                        video[(redraw+k)*2+1] = 0x0F;
                    }
                    video[(line_start+cmd_len)*2] = ' ';
                    video[(line_start+cmd_len)*2+1] = 0x07;
                    unsigned short pos = cursor;
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                }
            }
            else if (c == '\t' && cursor < 80*25 - 4) {
                // Tab completion
                int old_cursor = cursor;
                handle_tab_completion(cmd_buf, &cmd_len, &cmd_cursor, video, &cursor, line_start);
                // If cursor moved to a new line (multiple matches shown), update line_start
                if (cursor / 80 > old_cursor / 80) {
                    line_start = (cursor / 80) * 80 + 2;  // Account for "> " prompt
                }
            }
            else {
                if (cursor % 80 == 0 && cursor != line_start && cursor < 80*25) {
                    if (cursor >= 80*25) {
                        scroll_screen(video);
                        cursor -= 80;
                    }
                }
                if (cursor < 80*25 - 1 && c != '\t') {
                    if (cmd_len < 63) {
                        for (int k = cmd_len; k > cmd_cursor; k--)
                            cmd_buf[k] = cmd_buf[k-1];
                        cmd_buf[cmd_cursor] = c;
                        cmd_len++;
                        int redraw = cursor;
                        for (int k = 0; k < cmd_len-cmd_cursor; k++) {
                            video[(redraw+k)*2] = cmd_buf[cmd_cursor+k];
                            video[(redraw+k)*2+1] = 0x0F;
                        }
                        cursor++;
                        cmd_cursor++;
                        unsigned short pos = cursor;
                        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                        asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                        asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                    }
                }
            }
        }
    }
}
