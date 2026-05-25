#include "kernel.h"

// --- Display Functions ---

#define SCREEN_COLS 80
#define SCREEN_ROWS 25
#define SCREEN_CELLS (SCREEN_COLS * SCREEN_ROWS)
#define CELL_BYTES 2
#define SCREEN_BYTES (SCREEN_CELLS * CELL_BYTES)
#define SCROLLBACK_LINES 1024

static char scrollback_buffer[SCROLLBACK_LINES][SCREEN_COLS * CELL_BYTES];
static int scrollback_start = 0;
static int scrollback_count = 0;
static int scrollback_offset = 0;
static char live_screen_snapshot[SCREEN_BYTES];
static char* capture_buffer = 0;
static int capture_max_len = 0;
static int capture_len = 0;
static int capture_suppress_screen = 0;

static int mouse_col = 40;
static int mouse_row = 12;
static int mouse_visible = 0;
static int mouse_saved_offset = -1;
static char mouse_saved_char = ' ';
static unsigned char mouse_saved_attr = COLOR_LIGHT_GRAY;

static void capture_append_char(char c) {
    if (!capture_buffer || capture_max_len <= 0) return;
    if (capture_len >= capture_max_len - 1) return;
    capture_buffer[capture_len++] = c;
    capture_buffer[capture_len] = 0;
}

static void capture_append_text(const char* str, int len, int prepend_newline) {
    if (!capture_buffer || !str) return;

    if (prepend_newline && capture_len > 0) {
        capture_append_char('\n');
    }

    for (int i = 0; i < len; ) {
        if (str[i] == '\\' && (i + 1 < len) && str[i + 1] == 'n') {
            capture_append_char('\n');
            i += 2;
            continue;
        }
        if (str[i] == '\n' || str[i] == 10) {
            capture_append_char('\n');
            i++;
            continue;
        }
        capture_append_char(str[i]);
        i++;
    }
}

static void copy_bytes(char* dst, const char* src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

static void save_live_screen(char* video) {
    copy_bytes(live_screen_snapshot, video, SCREEN_BYTES);
}

static void restore_live_screen_bytes(char* video) {
    copy_bytes(video, live_screen_snapshot, SCREEN_BYTES);
}

static void append_scrollback_row(const char* video) {
    int row_index = (scrollback_start + scrollback_count) % SCROLLBACK_LINES;
    copy_bytes(scrollback_buffer[row_index], video, SCREEN_COLS * CELL_BYTES);
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    } else {
        scrollback_start = (scrollback_start + 1) % SCROLLBACK_LINES;
    }
}

static const char* get_scrollback_row(int index) {
    if (index < 0 || index >= scrollback_count) return 0;
    return scrollback_buffer[(scrollback_start + index) % SCROLLBACK_LINES];
}

static void clear_mouse_overlay(char* video) {
    if (!mouse_visible || mouse_saved_offset < 0) return;
    video[mouse_saved_offset] = mouse_saved_char;
    video[mouse_saved_offset + 1] = mouse_saved_attr;
    mouse_visible = 0;
    mouse_saved_offset = -1;
}

static void draw_mouse_overlay(char* video) {
    int cell = mouse_row * SCREEN_COLS + mouse_col;
    int offset = cell * CELL_BYTES;
    mouse_saved_char = video[offset];
    mouse_saved_attr = (unsigned char)video[offset + 1];
    mouse_saved_offset = offset;
    video[offset] = ' '; //all we use it for is scrolling so i got rid of the X, can put it back later when we have a real GUI
    video[offset + 1] = COLOR_LIGHT_CYAN;
    mouse_visible = 1;
}

static void render_scrollback_view(char* video) {
    int top_line = scrollback_count - scrollback_offset;
    if (top_line < 0) top_line = 0;

    for (int row = 0; row < SCREEN_ROWS; row++) {
        int source_line = top_line + row;
        int dst_offset = row * SCREEN_COLS * CELL_BYTES;

        if (source_line < scrollback_count) {
            const char* src = get_scrollback_row(source_line);
            if (src) {
                copy_bytes(&video[dst_offset], src, SCREEN_COLS * CELL_BYTES);
                continue;
            }
        }

        source_line -= scrollback_count;
        if (source_line >= 0 && source_line < SCREEN_ROWS) {
            copy_bytes(&video[dst_offset], &live_screen_snapshot[source_line * SCREEN_COLS * CELL_BYTES], SCREEN_COLS * CELL_BYTES);
        } else {
            for (int col = 0; col < SCREEN_COLS; col++) {
                video[dst_offset + col * CELL_BYTES] = ' ';
                video[dst_offset + col * CELL_BYTES + 1] = COLOR_LIGHT_GRAY;
            }
        }
    }
}

void display_init(char* video) {
    scrollback_start = 0;
    scrollback_count = 0;
    scrollback_offset = 0;
    mouse_visible = 0;
    mouse_saved_offset = -1;
    save_live_screen(video);
}

void display_hide_mouse(char* video) {
    clear_mouse_overlay(video);
}

void display_refresh_mouse(char* video) {
    clear_mouse_overlay(video);
    draw_mouse_overlay(video);
}

void display_set_mouse_position(int col, int row) {
    if (col < 0) col = 0;
    if (col >= SCREEN_COLS) col = SCREEN_COLS - 1;
    if (row < 0) row = 0;
    if (row >= SCREEN_ROWS) row = SCREEN_ROWS - 1;
    mouse_col = col;
    mouse_row = row;
}

int display_scroll_view(int delta, char* video) {
    if (scrollback_count <= 0) {
        scrollback_offset = 0;
        return 0;
    }

    int next_offset = scrollback_offset - delta; //make it + delta to swithc scroll direction
    if (next_offset < 0) next_offset = 0;
    if (next_offset > scrollback_count) next_offset = scrollback_count;

    if (scrollback_offset == 0 && next_offset > 0) {
        clear_mouse_overlay(video);
        save_live_screen(video);
    }

    scrollback_offset = next_offset;
    if (scrollback_offset == 0) {
        restore_live_screen_bytes(video);
    } else {
        render_scrollback_view(video);
    }

    return scrollback_offset;
}

int display_is_scrollback_active(void) {
    return scrollback_offset > 0;
}

void display_sync_live_screen(char* video) {
    if (scrollback_offset != 0) return;
    clear_mouse_overlay(video);
    save_live_screen(video);
}

void display_restore_live_screen(char* video) {
    scrollback_offset = 0;
    clear_mouse_overlay(video);
    restore_live_screen_bytes(video);
}

void display_begin_capture(char* buffer, int max_len, int suppress_screen) {
    capture_buffer = buffer;
    capture_max_len = max_len;
    capture_len = 0;
    capture_suppress_screen = suppress_screen;
    if (capture_buffer && capture_max_len > 0) {
        capture_buffer[0] = 0;
    }
}

int display_end_capture(void) {
    int out_len = capture_len;
    capture_buffer = 0;
    capture_max_len = 0;
    capture_len = 0;
    capture_suppress_screen = 0;
    return out_len;
}

void scroll_screen(char* video) {
    clear_mouse_overlay(video);
    append_scrollback_row(video);
    //move all lines up by one
    for (int row = 1; row < SCREEN_ROWS; row++) {
        for (int col = 0; col < SCREEN_COLS; col++) {
            video[((row-1)*SCREEN_COLS+col)*2] = video[(row*SCREEN_COLS+col)*2];
            video[((row-1)*SCREEN_COLS+col)*2+1] = video[(row*SCREEN_COLS+col)*2+1];
        }
    }
    // clear last line
    for (int col = 0; col < SCREEN_COLS; col++) {
        video[((SCREEN_ROWS-1)*SCREEN_COLS+col)*2] = ' ';
        video[((SCREEN_ROWS-1)*SCREEN_COLS+col)*2+1] = 0x07;
    }
}

void print_smiggles_art(char* video, int* cursor) {
    const char* smiggles_art[6] = {
        "               _             _           ",
        " ___ _ __ ___ (_) __ _  __ _| | ___  ___ ",
        "/ __| '_ ` _ \\| |/ _` |/ _` | |/ _ \\/ __|",
        "\\__ \\ | | | | | | (_| | (_| | |  __/\\__ \\",
        "|___/_| |_| |_|_|\\__, |\\__, |_|\\___||___/",
        "                 |___/ |___/             "
    };
    int art_lines = 6;
    for (int l = 0; l < art_lines; l++) {
        for (int j = 0; smiggles_art[l][j] && j < 80; j++) {
            video[(l*80+j)*2] = smiggles_art[l][j];
            video[(l*80+j)*2+1] = COLOR_GREEN;
        }
    }
    *cursor = art_lines * 80;
}

void print_boot_splash(char* video) {
    // Clear screen
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i + 1] = COLOR_BLACK;
    }
    
    // Snake logo (left side)
    const char* snake_logo[12] = {
        "",
        "",
        "           /^/^-\\",
        "         _|__|  O|",
        "\\/     /~     \\_/ \\",
        " \\____|__________/  \\",
        "        \\_______      \\",
        "                `\\     \\",
        "                  |     |",
        "                 /      /",
        "                /     /",
        "              /      /"
    };
    
    // Smiggles ASCII art (right side)
    const char* smiggles_art[6] = {
        "               _             _           ",
        " ___ _ __ ___ (_) __ _  __ _| | ___  ___ ",
        "/ __| '_ ` _ \\| |/ _` |/ _` | |/ _ \\/ __|",
        "\\__ \\ | | | | | | (_| | (_| | |  __/\\__ \\",
        "|___/_| |_| |_|_|\\__, |\\__, |_|\\___||___/",
        "                 |___/ |___/             "
    };
    
    // Center the combined artwork on screen
    // Snake is ~20 chars wide, smiggles is ~42 chars wide, gap is ~4 chars
    // Total: ~66 chars, centered on 80 char width = starting at column 7
    int start_col = 6;
    int start_row = 5;
    
    // Print snake logo (left part)
    for (int l = 0; l < 12; l++) {
        for (int j = 0; snake_logo[l][j] && j < 25; j++) {
            int col = start_col + j;
            if (col < 80) {
                int offset = ((start_row - 1 + l) * 80 + col) * 2;
                video[offset] = snake_logo[l][j];
                video[offset + 1] = COLOR_GREEN;
            }
        }
    }
    
    // Print smiggles art on right (with gap after snake)
    int smiggles_start_col = start_col + 25 + 4;  // 25 for snake + 4 for gap
    for (int l = 0; l < 6; l++) {
        for (int j = 0; smiggles_art[l][j] && j < 42; j++) {
            int col = smiggles_start_col + j;
            if (col < 80) {
                int offset = ((start_row + l) * 80 + col) * 2;
                video[offset] = smiggles_art[l][j];
                video[offset + 1] = COLOR_GREEN;
            }
        }
    }
    
    // Print boot message at bottom
    const char* boot_msg_base = "Booting Smiggles OS";
    int msg_col = (80 - 19) / 2;  // Center the message
    for (int j = 0; boot_msg_base[j] && j < 19; j++) {
        int offset = (20 * 80 + msg_col + j) * 2;
        video[offset] = boot_msg_base[j];
        video[offset + 1] = COLOR_GREEN;
    }
}

//print a string on NEW LINE with color
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color) {
    int computed_len = len;
    if (computed_len < 0) {
        computed_len = 0;
        while (str[computed_len]) computed_len++;
    }

    if (capture_buffer) {
        capture_append_text(str, computed_len, 1);
        if (capture_suppress_screen) return;
    }

    *cursor = ((*cursor / 80) + 1) * 80; //this is what goes to the new line
    while (*cursor >= 80*25) {
        scroll_screen(video);
        *cursor -= 80;
    }
    // If len < 0, auto-calculate string length
    len = computed_len;
    for (int i = 0; i < len; ) {
        // Handle "\\n" (two-character sequence)
        if (str[i] == '\\' && (i+1 < len) && str[i+1] == 'n') {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i += 2;
            continue;
        }
        // Handle actual newline character (char 10)
        if (str[i] == '\n' || str[i] == 10) {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i++;
            continue;
        }
        while (*cursor >= 80*25) {
            scroll_screen(video);
            *cursor -= 80;
        }
        video[(*cursor)*2] = str[i];
        video[(*cursor)*2+1] = color;
        (*cursor)++;
        i++;
    }
}

//print string on SAME LINE with color
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color) {
    int computed_len = len;
    if (computed_len < 0) {
        computed_len = 0;
        while (str[computed_len]) computed_len++;
    }

    if (capture_buffer) {
        capture_append_text(str, computed_len, 0);
        if (capture_suppress_screen) return;
    }

    // If len < 0, auto-calculate string length
    len = computed_len;
    for (int i = 0; i < len; ) {
        // Handle "\\n" (two-character sequence)
        if (str[i] == '\\' && (i+1 < len) && str[i+1] == 'n') {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i += 2;
            continue;
        }
        // Handle actual newline character (char 10)
        if (str[i] == '\n' || str[i] == 10) {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i++;
            continue;
        }
        while (*cursor >= 80*25) {
            scroll_screen(video);
            *cursor -= 80;
        }
        video[(*cursor)*2] = str[i];
        video[(*cursor)*2+1] = color;
        (*cursor)++;
        i++;
    }
}

// --- Shared Keyboard and Display Utilities ---
// Simple get_key implementation for login input
char get_key(void) {
    unsigned char scancode = 0;
    int shift = 0;
    int e0_prefix_pending = 0;
    while (1) {
        if (keyboard_pop_scancode(&scancode)) {
            // Handle shift
            if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
            if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }

            // Handle E0-prefixed extended keys (e.g. numpad Enter)
            if (scancode == 0xE0) {
                e0_prefix_pending = 1;
                continue;
            }

            if (e0_prefix_pending) {
                e0_prefix_pending = 0;
                if (scancode & 0x80) continue;
                if (scancode == 0x1C) return '\n';
                if (scancode == 0x35) return '/';
                continue;
            }

            // Ignore release codes
            if (scancode > 0x80) continue;
            char c = scancode_to_char(scancode, shift);
            if (c) return c;
        }
    }
}

// Scancode to character conversion tables
static const char lower_table[128] = {
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
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.',
};

static const char upper_table[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ', [0x0E] = 8,
    [0x0F] = '\t',
    [0x37] = '*', [0x4A] = '-', [0x4E] = '+',
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.',
};

// Set VGA hardware cursor position
void set_cursor_position(int cursor) {
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

// --- Numpad scancode handling ---
// Only map numpad keys if not E0-prefixed (arrow keys, etc. use E0)
char scancode_to_char(unsigned char scancode, int shift) {
    if (shift) {
        return upper_table[scancode];
    } else {
        return lower_table[scancode];
    }
}