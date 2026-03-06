#include "kernel.h"

static int fs_state_is_valid(void) {
    if (!node_table[0].used) return 0;
    if (node_table[0].type != NODE_DIRECTORY) return 0;
    if (node_table[0].parent_idx != -1) return 0;

    if (current_dir_idx < 0 || current_dir_idx >= MAX_NODES) return 0;
    if (!node_table[current_dir_idx].used) return 0;
    if (node_table[current_dir_idx].type != NODE_DIRECTORY) return 0;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!node_table[i].used) continue;
        if (i == 0) continue;
        int parent = node_table[i].parent_idx;
        if (parent < 0 || parent >= MAX_NODES) return 0;
        if (!node_table[parent].used) return 0;
        if (node_table[parent].type != NODE_DIRECTORY) return 0;
    }

    return 1;
}

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
    // Initialize basic paging and frame allocator (virtual memory foundation)
    init_paging();
    init_protection();
    init_process_table();

    // Load filesystem image from disk and validate; if invalid, initialize a new one
    fs_load();
    if (!fs_state_is_valid()) {
        init_filesystem();
        fs_save();
    }

    // --- Interrupt setup ---
    pic_remap();
    set_idt_entry(0x20, (unsigned int)irq0_timer_handler);
    set_idt_entry(0x21, (unsigned int)irq1_keyboard_handler);
    set_idt_entry_user(0x80, (unsigned int)isr_syscall_handler);
    // Unmask IRQ0 (timer) and IRQ1 (keyboard)
    asm volatile("outb %0, %1" : : "a"((unsigned char)0xFC), "Nd"((uint16_t)PIC1_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0xFF), "Nd"((uint16_t)PIC2_DATA));
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

    
    set_cursor_position(cursor);

    
    char cmd_buf[MAX_CMD_BUFFER];
    // Use global cmd_len and cmd_cursor so nano_editor can reset them
    // int cmd_len = 0;
    // int cmd_cursor = 0; // position within the input line

    // Enable cursor
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0A), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0B), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)15), "Nd"((unsigned short)0x3D5));

    // Continue with normal kernel loop
    while (1) {
        unsigned char scancode;
        if (!keyboard_pop_scancode(&scancode)) {
            continue;
        }

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
                if (!keyboard_pop_scancode(&next_scancode)) {
                    continue;
                }
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
                    while (tab_matches[tab_completion_position][j] && cmd_len < (MAX_CMD_BUFFER - 1)) {
                        cmd_buf[cmd_len] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2+1] = 0x0F;
                        cmd_len++;
                        j++;
                    }
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    set_cursor_position(cursor);
                } else if (cmd_cursor > 0) {
                    cmd_cursor--;
                    cursor--;
                    set_cursor_position(cursor);
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    if (!keyboard_pop_scancode(&rel)) {
                        continue;
                    }
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
                    while (tab_matches[tab_completion_position][j] && cmd_len < (MAX_CMD_BUFFER - 1)) {
                        cmd_buf[cmd_len] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2] = tab_matches[tab_completion_position][j];
                        video[(line_start + cmd_len)*2+1] = 0x0F;
                        cmd_len++;
                        j++;
                    }
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    set_cursor_position(cursor);
                } else if (cmd_cursor < cmd_len) {
                    cmd_cursor++;
                    cursor++;
                    set_cursor_position(cursor);
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    if (!keyboard_pop_scancode(&rel)) {
                        continue;
                    }
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
                    while (history[history_position][cmd_len] && cmd_len < (MAX_CMD_BUFFER - 1)) {
                        cmd_buf[cmd_len] = history[history_position][cmd_len];
                        video[(line_start + cmd_len)*2] = cmd_buf[cmd_len];
                        video[(line_start + cmd_len)*2+1] = 0x0F;
                        cmd_len++;
                    }
                    cmd_cursor = cmd_len;
                    cursor = line_start + cmd_len;
                    
                    set_cursor_position(cursor);
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    if (!keyboard_pop_scancode(&rel)) {
                        continue;
                    }
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
                        while (history[history_position][cmd_len] && cmd_len < (MAX_CMD_BUFFER - 1)) {
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
                    
                    set_cursor_position(cursor);
                }
                // Wait for key release
                while (1) {
                    unsigned char rel;
                    if (!keyboard_pop_scancode(&rel)) {
                        continue;
                    }
                    if (rel == 0xD0) break; // Release code for down arrow
                }
                continue;
            }
            continue; // Ignore other E0 keys
        }

        char c = scancode_to_char(scancode, shift);

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
                while (cursor >= 80*25) {
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
                set_cursor_position(cursor);
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
                    set_cursor_position(cursor);
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
                if (c != '\t' && cmd_len < (MAX_CMD_BUFFER - 1)) {
                    if (cmd_cursor == cmd_len) {
                        if (cursor >= 80*25) {
                            scroll_screen(video);
                            cursor -= 80;
                            if (line_start >= 80) line_start -= 80;
                            else line_start = 0;
                        }

                        video[cursor*2] = c;
                        video[cursor*2+1] = 0x0F;
                        cmd_buf[cmd_cursor] = c;
                        cmd_len++;
                        cmd_cursor++;
                        cursor++;

                        if (cursor >= 80*25) {
                            scroll_screen(video);
                            cursor -= 80;
                            if (line_start >= 80) line_start -= 80;
                            else line_start = 0;
                        }

                        set_cursor_position(cursor);
                    } else {
                        if (cursor >= 80*25) {
                            scroll_screen(video);
                            cursor -= 80;
                            if (line_start >= 80) line_start -= 80;
                            else line_start = 0;
                        }

                        for (int k = cmd_len; k > cmd_cursor; k--)
                            cmd_buf[k] = cmd_buf[k-1];
                        cmd_buf[cmd_cursor] = c;
                        cmd_len++;

                        int redraw = cursor;
                        for (int k = 0; k < cmd_len-cmd_cursor; k++) {
                            if (redraw + k >= 80*25) break;
                            video[(redraw+k)*2] = cmd_buf[cmd_cursor+k];
                            video[(redraw+k)*2+1] = 0x0F;
                        }
                        cursor++;
                        cmd_cursor++;
                        set_cursor_position(cursor);
                    }
                }
            }
        }
    }
}
