#include "kernel.h"

static void panic_clear_screen(char* video, unsigned char color) {
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = color;
    }
}

static void panic_write_at(char* video, int row, int col, const char* text, unsigned char color) {
    int index = 0;
    int cell = row * 80 + col;

    if (!text || row < 0 || row >= 25 || col < 0 || col >= 80) return;

    while (text[index] && (col + index) < 80) {
        video[(cell + index) * 2] = text[index];
        video[(cell + index) * 2 + 1] = color;
        index++;
    }
}

static void panic_append_dec(char* buf, int value) {
    char temp[16];
    int_to_str(value, temp);
    str_concat(buf, temp);
}

static void panic_clear_line(char* video, int row, unsigned char color) {
    if (row < 0 || row >= 25) return;
    for (int col = 0; col < 80; col++) {
        int offset = (row * 80 + col) * 2;
        video[offset] = ' ';
        video[offset + 1] = color;
    }
}

static void panic_hide_mouse(char* video, int* visible, int* saved_offset, char* saved_char, unsigned char* saved_attr) {
    if (!*visible || *saved_offset < 0) return;
    video[*saved_offset] = *saved_char;
    video[*saved_offset + 1] = *saved_attr;
    *visible = 0;
    *saved_offset = -1;
}

static void panic_draw_mouse(char* video, int col, int row, int* visible, int* saved_offset, char* saved_char, unsigned char* saved_attr) {
    int offset;

    if (col < 0) col = 0;
    if (col > 79) col = 79;
    if (row < 0) row = 0;
    if (row > 24) row = 24;

    offset = (row * 80 + col) * 2;
    *saved_char = video[offset];
    *saved_attr = (unsigned char)video[offset + 1];
    *saved_offset = offset;
    video[offset] = 'X';
    video[offset + 1] = COLOR_LIGHT_CYAN;
    *visible = 1;
}

static void panic_reboot(void) {
    asm volatile("cli");
    outb(0x64, 0xFE);
    while (1) {
        asm volatile("hlt");
    }
}

static void panic_poweroff(void) {
    asm volatile("outw %0, %1" : : "a"((unsigned short)0x2000), "Nd"((unsigned short)0x604));
    while (1) {
        asm volatile("hlt");
    }
}

static void panic_command_loop(char* video) {
    char cmd[16];
    int cmd_len = 0;
    int shift = 0;
    int e0_prefix = 0;
    int mouse_col = 40;
    int mouse_row = 12;
    int mouse_visible = 0;
    int mouse_saved_offset = -1;
    char mouse_saved_char = ' ';
    unsigned char mouse_saved_attr = COLOR_LIGHT_RED;
    MouseState mouse_state;

    panic_write_at(video, 18, 2, "Type 'reboot' or 'halt' and press Enter.", COLOR_WHITE);
    panic_write_at(video, 20, 2, "> ", COLOR_YELLOW);
    panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
    set_cursor_position(20 * 80 + 4);

    while (1) {
        mouse_poll_hardware();
        if (mouse_poll_state(&mouse_state)) {
            panic_hide_mouse(video, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
            mouse_col = mouse_state.col;
            mouse_row = mouse_state.row;
            panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
            set_cursor_position(20 * 80 + 4 + cmd_len);
        }

        unsigned char status = inb(0x64);
        if ((status & 0x01) == 0) {
            continue;
        }

        if (status & 0x20) {
            mouse_poll_hardware();
            if (mouse_poll_state(&mouse_state)) {
                panic_hide_mouse(video, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
                mouse_col = mouse_state.col;
                mouse_row = mouse_state.row;
                panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
                set_cursor_position(20 * 80 + 4 + cmd_len);
            }
            continue;
        }

        unsigned char scancode = inb(0x60);

        if (scancode == 0xE0) {
            e0_prefix = 1;
            continue;
        }

        if (scancode == 0x2A || scancode == 0x36) {
            shift = 1;
            continue;
        }
        if (scancode == 0xAA || scancode == 0xB6) {
            shift = 0;
            continue;
        }

        if (e0_prefix) {
            e0_prefix = 0;
            continue;
        }

        if (scancode & 0x80) {
            continue;
        }

        {
            char c = scancode_to_char(scancode, shift);
            if (!c) continue;

            panic_hide_mouse(video, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);

            if (c == 8) {
                if (cmd_len > 0) {
                    cmd_len--;
                    cmd[cmd_len] = 0;
                    panic_write_at(video, 20, 4 + cmd_len, " ", COLOR_WHITE);
                    set_cursor_position(20 * 80 + 4 + cmd_len);
                }
                panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
                set_cursor_position(20 * 80 + 4 + cmd_len);
                continue;
            }

            if (c == '\n') {
                cmd[cmd_len] = 0;

                if (mini_strcmp(cmd, "reboot") == 0) {
                    panic_reboot();
                }
                if (mini_strcmp(cmd, "halt") == 0) {
                    panic_poweroff();
                }

                panic_clear_line(video, 22, COLOR_LIGHT_RED);
                panic_write_at(video, 22, 2, "Unknown command. Use 'reboot' or 'halt'.", COLOR_YELLOW);
                panic_clear_line(video, 20, COLOR_LIGHT_RED);
                panic_write_at(video, 20, 2, "> ", COLOR_YELLOW);
                cmd_len = 0;
                cmd[0] = 0;
                panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
                set_cursor_position(20 * 80 + 4);
                continue;
            }

            if (c >= 32 && c <= 126 && cmd_len < (int)sizeof(cmd) - 1) {
                cmd[cmd_len++] = c;
                cmd[cmd_len] = 0;
                {
                    char out[2];
                    out[0] = c;
                    out[1] = 0;
                    panic_write_at(video, 20, 3 + cmd_len, out, COLOR_WHITE);
                }
                panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
                set_cursor_position(20 * 80 + 4 + cmd_len);
            } else {
                panic_draw_mouse(video, mouse_col, mouse_row, &mouse_visible, &mouse_saved_offset, &mouse_saved_char, &mouse_saved_attr);
                set_cursor_position(20 * 80 + 4 + cmd_len);
            }
        }
    }
}

void kernel_panic(const char* reason, const char* detail) {
    char* video = (char*)0xB8000;
    char buf[64];

    asm volatile("cli");

    panic_clear_screen(video, COLOR_LIGHT_RED);

    panic_write_at(video, 1, 2, "---CRASHY SCREEN---", COLOR_WHITE);
    panic_write_at(video, 3, 2, "The system encountered a fatal error and was stopped.", COLOR_WHITE);

    if (reason && reason[0]) {
        panic_write_at(video, 5, 2, "Reason:", COLOR_YELLOW);
        panic_write_at(video, 6, 4, reason, COLOR_WHITE);
    }

    if (detail && detail[0]) {
        panic_write_at(video, 8, 2, "Detail:", COLOR_YELLOW);
        panic_write_at(video, 9, 4, detail, COLOR_WHITE);
    }

    buf[0] = 0;
    str_concat(buf, "Ticks: ");
    panic_append_dec(buf, ticks);
    panic_write_at(video, 11, 2, buf, COLOR_LIGHT_CYAN);

    buf[0] = 0;
    str_concat(buf, "Current process: ");
    panic_append_dec(buf, current_process);
    panic_write_at(video, 12, 2, buf, COLOR_LIGHT_CYAN);

    buf[0] = 0;
    str_concat(buf, "Last key/scancode: ");
    panic_append_dec(buf, (unsigned char)last_key);
    panic_write_at(video, 13, 2, buf, COLOR_LIGHT_CYAN);

    panic_write_at(video, 15, 2, "System halted. Reboot required.", COLOR_YELLOW);

    set_cursor_position(0);

    panic_command_loop(video);
}

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
// --- User Authentication ---
User user_table[MAX_USERS] = {
    {"admin", "admin", 1},
    {"user", "password", 0}
};
int user_count = 2;

int current_user_idx = -1; // -1 means no user logged in




// commented out login screen for now
/*
int request_login_screen = 0;

void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);

static void clear_vga_screen(char* video, int* cursor) {
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    *cursor = 0;
    set_cursor_position(*cursor);
}

static void boot_loading_animation(char* video, int* cursor) {
    print_string("Loading main kernel", -1, video, cursor, COLOR_LIGHT_GRAY);

    for (int i = 0; i < 6; i++) {
        unsigned int target = (unsigned int)ticks + 6;
        while ((unsigned int)ticks < target) {
            // wait ~0.33s per step at 18.2Hz timer
        }
        print_string_sameline(".", 1, video, cursor, COLOR_LIGHT_GRAY);
    }
}

static void boot_login_screen(char* video, int* cursor) {
    char username[MAX_NAME_LENGTH];
    char password[MAX_NAME_LENGTH];

    clear_vga_screen(video, cursor);
    print_string("--- Smiggles Login ---", -1, video, cursor, COLOR_LIGHT_CYAN);
    print_string("Login required.", -1, video, cursor, COLOR_YELLOW);

    while (current_user_idx < 0) {
        shell_read_line("Username: ", username, MAX_NAME_LENGTH, video, cursor);
        shell_read_line("Password: ", password, MAX_NAME_LENGTH, video, cursor);

        for (int i = 0; i < user_count; i++) {
            if (mini_strcmp(username, user_table[i].username) == 0 &&
                mini_strcmp(password, user_table[i].password) == 0) {
                current_user_idx = i;
                break;
            }
        }

        if (current_user_idx >= 0) {
            print_string("Login successful!", -1, video, cursor, COLOR_LIGHT_GREEN);
            fs_enter_user_home();
            boot_loading_animation(video, cursor);
        } else {
            print_string("Login failed.", -1, video, cursor, COLOR_LIGHT_RED);
        }
    }
}

static void draw_main_shell_screen(char* video, int* cursor, int* line_start_out) {
    clear_vga_screen(video, cursor);
    print_smiggles_art(video, cursor);
    *cursor += 80;

    const char* prompt = "> ";
    int pi = 0;
    while (prompt[pi] && *cursor < 80 * 25 - 1) {
        video[(*cursor) * 2] = prompt[pi];
        video[(*cursor) * 2 + 1] = 0x0F;
        (*cursor)++;
        pi++;
    }
    set_cursor_position(*cursor);
    *line_start_out = *cursor;
}




*/








// Reusable shell_read_line for login and prompts
void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor) {
    // Move cursor to new line for prompt
    *cursor = ((*cursor / 80) + 1) * 80;
    while (*cursor >= 80*25) {
        scroll_screen(video);
        *cursor -= 80;
    }
    int pi = 0;
    while (prompt[pi] && *cursor < 80*25 - 1) {
        video[(*cursor)*2] = prompt[pi];
        video[(*cursor)*2+1] = 0x0F;
        (*cursor)++;
        pi++;
    }
    set_cursor_position(*cursor);
    int line_start = *cursor;
    int len = 0;
    int cmd_cursor = 0;
    int shift = 0;
    int e0_prefix_pending = 0;
    while (1) {
        unsigned char scancode;
        if (!keyboard_pop_scancode(&scancode)) continue;
        if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
        if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }

        if (scancode == 0xE0) {
            e0_prefix_pending = 1;
            continue;
        }

        if (e0_prefix_pending) {
            e0_prefix_pending = 0;
            if (scancode & 0x80) continue;
            if (scancode == 0x1C) scancode = 0x1C;
            else if (scancode == 0x35) scancode = 0x35;
            else continue;
        }

        if (scancode > 0x80) continue;
        char c = scancode_to_char(scancode, shift);
        if (!c) continue;
        if (c == '\n') break;
        if (c == 8 && cmd_cursor > 0 && len > 0 && *cursor > line_start) {
            for (int k = cmd_cursor-1; k < len-1; k++) buf[k] = buf[k+1];
            len--;
            cmd_cursor--;
            (*cursor)--;
            int redraw = *cursor;
            for (int k = 0; k < len-cmd_cursor; k++) {
                video[(redraw+k)*2] = buf[cmd_cursor+k];
                video[(redraw+k)*2+1] = 0x0F;
            }
            video[(line_start+len)*2] = ' ';
            video[(line_start+len)*2+1] = 0x07;
            set_cursor_position(*cursor);
            continue;
        }
        if (c >= 32 && c <= 126 && len < max_len-1) {
            buf[cmd_cursor] = c;
            len++;
            cmd_cursor++;
            (*cursor)++;
            video[(*cursor-1)*2] = c;
            video[(*cursor-1)*2+1] = 0x0F;
            set_cursor_position(*cursor);
        }
    }
    buf[len] = 0;
}

void kernel_main(void) {
        // Automatically log in as admin at boot
        current_user_idx = 0;
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
    // fsimage_to_globals resets current_user_idx, so set it again after fs_load/init
    current_user_idx = 0;

    // --- Interrupt setup ---
    pic_remap();
    for (int vector = 0; vector < 32; vector++) {
        set_idt_entry(vector, (unsigned int)exception_stub_table[vector]);
    }
    set_idt_entry(0x20, (unsigned int)irq0_timer_handler);
    set_idt_entry(0x21, (unsigned int)irq1_keyboard_handler);
    set_idt_entry(0x2C, (unsigned int)irq12_mouse_handler);
    set_idt_entry_user(0x80, (unsigned int)isr_syscall_handler);
    // Unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade), and IRQ12 (mouse)
    asm volatile("outb %0, %1" : : "a"((unsigned char)0xF8), "Nd"((uint16_t)PIC1_DATA));
    asm volatile("outb %0, %1" : : "a"((unsigned char)0xEF), "Nd"((uint16_t)PIC2_DATA));
    extern struct IDT_ptr idt_ptr;
    extern struct IDT_entry idt[256];
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (unsigned int)&idt;
    load_idt(&idt_ptr);
    mouse_init();
    asm volatile("sti");
    char* video = (char*)0xB8000;
    int cursor = 0;
    int prompt_end = 0;
    int line_start = 0;
    unsigned char prev_scancode = 0;
    int e0_prefix_pending = 0;
    int shift = 0;

    
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }
    display_init(video);

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

    display_set_mouse_position(40, 12);
    display_sync_live_screen(video);
    display_refresh_mouse(video);

    // Continue with normal kernel loop
    while (1) {
        MouseState mouse_state;
        unsigned char scancode;

        mouse_poll_hardware();

        if (mouse_poll_state(&mouse_state)) {
            display_hide_mouse(video);
            if (mouse_state.wheel_delta != 0) {
                display_scroll_view(mouse_state.wheel_delta, video);
            }
            display_set_mouse_position(mouse_state.col, mouse_state.row);
            display_sync_live_screen(video);
            display_refresh_mouse(video);
        }

        if (!keyboard_pop_scancode(&scancode)) {
            continue;
        }

        display_hide_mouse(video);
        if (display_is_scrollback_active()) {
            display_restore_live_screen(video);
        }

        //SHIFT KEYS
        if (scancode == 0x2A || scancode == 0x36) { 
            shift = 1;
            display_refresh_mouse(video);
            continue;
        }
        if (scancode == 0xAA || scancode == 0xB6) { 
            shift = 0;
            display_refresh_mouse(video);
            continue;
        }

        // Handle E0 prefix for extended keys (arrow keys, etc.)
        if (scancode == 0xE0) {
            e0_prefix_pending = 1;
            display_refresh_mouse(video);
            continue;
        }

        int e0_prefix = e0_prefix_pending;
        e0_prefix_pending = 0;

        // Ignore release codes for E0-prefixed keys
        if (e0_prefix && (scancode & 0x80)) {
            display_refresh_mouse(video);
            continue;
        }

        // Filter out release codes for non-E0 keys
        if (!e0_prefix && scancode > 0x80) {
            prev_scancode = 0;
            display_refresh_mouse(video);
            continue;
        }
        
        // For arrow keys, don't check prev_scancode to allow repeated presses
        if (!e0_prefix) {
            if ((scancode == prev_scancode && scancode != 0x0E) || scancode == 0) {
                display_refresh_mouse(video);
                continue;
            }
            prev_scancode = scancode;
        }

        if (e0_prefix) {
            if (scancode == 0x1C || scancode == 0x35) {
                // Numpad Enter and numpad slash should be processed like normal keys
            } else if (scancode == 0x4B) { // Left arrow
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
                display_refresh_mouse(video);
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
                display_refresh_mouse(video);
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
                display_refresh_mouse(video);
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
                display_refresh_mouse(video);
                continue;
            }
            else {
                display_refresh_mouse(video);
                continue; // Ignore other E0 keys
            }
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

        display_sync_live_screen(video);
        display_refresh_mouse(video);
    }
}