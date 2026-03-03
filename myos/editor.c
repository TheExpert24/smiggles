#include "kernel.h"

// --- Global Variables ---
// file_table and file_count removed; use node_table only

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
        // Re-resolve node_idx after creation
        node_idx = resolve_path(filename);
        if (node_idx == -1 || node_table[node_idx].type != NODE_FILE) {
            print_string("File error", 10, video, cursor, 0xC);
            return;
        }
    }
    char* buf = node_table[node_idx].content;
    // Save the current screen and cursor
    char prev_screen[80*25*2];
    for (int i = 0; i < 80*25*2; ++i) prev_screen[i] = video[i];
    int prev_cursor = *cursor;
    int pos = node_table[node_idx].content_size;
    if (pos < 0 || pos > MAX_FILE_CONTENT - 1) pos = 0;
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
    set_cursor_position(*cursor);
    
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
        if (ctrl && scancode == 0x1F) { // Ctrl+S: Save
            if (pos < 0) pos = 0;
            if (pos > MAX_FILE_CONTENT - 1) pos = MAX_FILE_CONTENT - 1;
            buf[pos] = 0;
            node_table[node_idx].content_size = pos;
            fs_save();
            while (1) {
                unsigned char sc;
                asm volatile("inb $0x60, %0" : "=a"(sc));
                if (sc == 0x9D) break;
            }
            exit_code = 1;
            break;
        }
        if (ctrl && scancode == 0x10) { // Ctrl+Q: Quit (do not save)
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
            char c = scancode_to_char(scancode, shift);
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
        set_cursor_position(*cursor);
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