#include "kernel.h"

#define EDIT_ROWS 22
#define EDIT_COLS 80

static char editor_work_buf[MAX_FILE_CONTENT];

static void logical_pos_for_index(const char* buf, int index, int* out_row, int* out_col) {
    int row = 0;
    int col = 0;
    for (int i = 0; i < index; i++) {
        if (buf[i] == '\n') {
            row++;
            col = 0;
        } else {
            col++;
            if (col >= EDIT_COLS) {
                row++;
                col = 0;
            }
        }
    }
    *out_row = row;
    *out_col = col;
}

static void render_editor_view(const char* buf, int len, char* video, int edit_start, int view_top_row) {
    char desired_chars[EDIT_ROWS * EDIT_COLS];
    unsigned char desired_attrs[EDIT_ROWS * EDIT_COLS];

    for (int i = 0; i < EDIT_ROWS * EDIT_COLS; i++) {
        desired_chars[i] = ' ';
        desired_attrs[i] = 0x07;
    }

    int logical_row = 0;
    int logical_col = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] != '\n') {
            if (logical_row >= view_top_row && logical_row < view_top_row + EDIT_ROWS) {
                int screen_row = logical_row - view_top_row;
                int cell = screen_row * EDIT_COLS + logical_col;
                desired_chars[cell] = buf[i];
                desired_attrs[cell] = 0x0F;
            }

            logical_col++;
            if (logical_col >= EDIT_COLS) {
                logical_row++;
                logical_col = 0;
            }
        } else {
            logical_row++;
            logical_col = 0;
        }
    }

    for (int r = 0; r < EDIT_ROWS; r++) {
        for (int c = 0; c < EDIT_COLS; c++) {
            int cell = r * EDIT_COLS + c;
            int idx = edit_start + cell;
            char current_char = video[idx * 2];
            unsigned char current_attr = (unsigned char)video[idx * 2 + 1];
            if (current_char != desired_chars[cell] || current_attr != desired_attrs[cell]) {
                video[idx * 2] = desired_chars[cell];
                video[idx * 2 + 1] = desired_attrs[cell];
            }
        }
    }
}

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
    // Save the current screen and cursor
    char prev_screen[80*25*2];
    for (int i = 0; i < 80*25*2; ++i) prev_screen[i] = video[i];
    int prev_cursor = *cursor;
    int pos = node_table[node_idx].content_size;
    if (pos < 0 || pos > MAX_FILE_CONTENT - 1) pos = 0;
    int editing = 1;
    int maxlen = MAX_FILE_CONTENT - 1;
    if (pos > maxlen) {
        print_string("Error: file too long, not saved", -1, video, cursor, COLOR_RED);
        // Remove the file
        node_table[node_idx].used = 0;
        fs_save();
        return;
    }
    for (int i = 0; i < pos; i++) {
        editor_work_buf[i] = node_table[node_idx].content[i];
    }
    editor_work_buf[pos] = 0;
    char* buf = editor_work_buf;
    
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    int header_cursor = 0;
    print_string("--- Smiggles Editor ---", -1, video, &header_cursor, COLOR_BROWN);
    print_string("Ctrl+S: Save | Ctrl+Q: Quit", -1, video, &header_cursor, COLOR_LIGHT_GRAY);
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

    int view_top_row = 0;
    int initial_row = 0;
    int initial_col = 0;
    logical_pos_for_index(buf, pos, &initial_row, &initial_col);
    if (initial_row >= EDIT_ROWS) {
        view_top_row = initial_row - EDIT_ROWS + 1;
    }
    render_editor_view(buf, pos, video, edit_start, view_top_row);
    int initial_screen_row = initial_row - view_top_row;
    int initial_pos = edit_start + initial_screen_row * EDIT_COLS + initial_col;
    if (initial_pos >= 80*25) initial_pos = 80*25 - 1;
    *cursor = initial_pos;
    set_cursor_position(*cursor);
    
    int shift = 0, ctrl = 0;
    unsigned char prev_scancode = 0;
    int exit_code = 0;
    
    while (editing) {
        unsigned char scancode;
        if (!keyboard_pop_scancode(&scancode)) {
            continue;
        }
        if (scancode & 0x80) {
            if (scancode == 0xAA || scancode == 0xB6) shift = 0;
            if (scancode == 0x9D) ctrl = 0;
            prev_scancode = 0;
            continue;
        }
        if ((scancode == prev_scancode && scancode != 0x0E) || scancode == 0) continue;
        prev_scancode = scancode;
        if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
        if (scancode == 0x1D) { ctrl = 1; continue; }
        if (ctrl && scancode == 0x1F) { // Ctrl+S: Save
            if (pos < 0) pos = 0;
            if (pos > maxlen) {
                print_string("Error: file too long, not saved", -1, video, cursor, COLOR_RED);
                node_table[node_idx].used = 0;
                fs_save();
                while (1) {
                    unsigned char sc;
                    if (!keyboard_pop_scancode(&sc)) {
                        continue;
                    }
                    if (sc == 0x9D) break;
                }
                exit_code = 2;
                break;
            }
            buf[pos] = 0;
            for (int i = 0; i <= pos; i++) {
                node_table[node_idx].content[i] = buf[i];
            }
            node_table[node_idx].content_size = pos;
            fs_save();
            while (1) {
                unsigned char sc;
                if (!keyboard_pop_scancode(&sc)) {
                    continue;
                }
                if (sc == 0x9D) break;
            }
            exit_code = 1;
            break;
        }
        if (ctrl && scancode == 0x10) { // Ctrl+Q: Quit (do not save)
            while (1) {
                unsigned char sc;
                if (!keyboard_pop_scancode(&sc)) {
                    continue;
                }
                if (sc == 0x9D) break;
            }
            exit_code = 2;
            break;
        }
        if (scancode == 0x1C && pos < maxlen) {
            buf[pos++] = '\n';
        }
        else if (scancode == 0x0E && pos > 0) {
            pos--;
        }
        else if (scancode < 128) {
            char c = scancode_to_char(scancode, shift);
            if (c && pos < maxlen) {
                buf[pos++] = c;
            }
        }
        int cur_row = 0;
        int cur_col = 0;
        logical_pos_for_index(buf, pos, &cur_row, &cur_col);

        if (cur_row < view_top_row) {
            view_top_row = cur_row;
        } else if (cur_row >= view_top_row + EDIT_ROWS) {
            view_top_row = cur_row - EDIT_ROWS + 1;
        }

        render_editor_view(buf, pos, video, edit_start, view_top_row);

        int screen_row = cur_row - view_top_row;
        if (screen_row < 0) screen_row = 0;
        if (screen_row >= EDIT_ROWS) screen_row = EDIT_ROWS - 1;

        int cur_pos = edit_start + screen_row * EDIT_COLS + cur_col;
        if (cur_pos >= 80*25) cur_pos = 80*25 - 1;
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
        if (!keyboard_pop_scancode(&dummy)) {
            break;
        }
        drain_count++;
        for (volatile int d = 0; d < 1000; d++);
    }
}