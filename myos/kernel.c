#include <stdint.h>
// --- Interrupts and IDT ---
#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

struct IDT_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed));

struct IDT_entry idt[256];
struct IDT_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed)) idt_ptr;

extern void load_idt(void*);
extern void irq0_timer_handler();
extern void irq1_keyboard_handler();

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

volatile int ticks = 0;
char last_key = 0;
int just_saved = 0;

// C handlers called from ASM stubs
void timer_handler() {
    ticks++;
    asm volatile("outb %0, %1" : : "a"((unsigned char)PIC_EOI), "Nd"((uint16_t)PIC1_COMMAND));
}
void keyboard_handler() {
    asm volatile("inb $0x60, %0" : "=a"(last_key));
    asm volatile("outb %0, %1" : : "a"((unsigned char)PIC_EOI), "Nd"((uint16_t)PIC1_COMMAND));
}
static void scroll_screen(char* video) {
    //move al lines up by one
    for (int row = 1; row < 25; row++) {
        for (int col = 0; col < 80; col++) {
            video[((row-1)*80+col)*2] = video[(row*80+col)*2];
            video[((row-1)*80+col)*2+1] = video[(row*80+col)*2+1];
        }
    }
    // ckear last line
    for (int col = 0; col < 80; col++) {
        video[((24)*80+col)*2] = ' ';
        video[((24)*80+col)*2+1] = 0x07;
    }
}


//ram file system
#define MAX_FILES 8
#define MAX_FILE_NAME 32
#define MAX_FILE_SIZE 128
#define MAX_DIRS 4
#define MAX_DIR_NAME 32

typedef struct {
    char name[MAX_DIR_NAME];
    int used;
    int parent; 
} RamDir;

static RamDir dir_table[MAX_DIRS] = { {"root", 1, -1} };
static int dir_count = 1;
static int current_dir = 0;


static void print_smiggles_art(char* video, int* cursor) {
    const char* smiggles_art[7] = {
        " _______  __   __  ___   _______  _______  ___      _______  _______ ",
        "|       ||  |_|  ||   | |       ||       ||   |    |       ||       |",
        "|  _____||       ||   | |    ___||    ___||   |    |    ___||  _____|",
        "| |_____ |       ||   | |   | __ |   | __ |   |    |   |___ | |_____ ",
        "|_____  ||       ||   | |   ||  ||   ||  ||   |___ |    ___||_____  |",
        " _____| || ||_|| ||   | |   |_| ||   |_| ||       ||   |___  _____| |",
        "|_______||_|   |_||___| |_______||_______||_______||_______||_______|"
    };
    unsigned char rainbow[7] = {0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E};
    int art_lines = 7;
    for (int l = 0; l < art_lines; l++) {
        for (int j = 0; smiggles_art[l][j] && j < 80; j++) {
            video[(l*80+j)*2] = smiggles_art[l][j];
            video[(l*80+j)*2+1] = rainbow[j % 7];
        }
    }
    *cursor = art_lines * 80;
}

//forward delcarations
static void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
static void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);



// Forward declarations for editor dependencies
static int find_file(const char* name);
static void handle_clear_command(char* video, int* cursor);

typedef struct {
    char name[MAX_FILE_NAME];
    char data[MAX_FILE_SIZE];
    int size;
    int dir; // 
} RamFile;
static RamFile file_table[MAX_FILES];
static int file_count = 0;

// --- Nano-like Text Editor ---
static void nano_editor(const char* filename, char* video, int* cursor) {
    int idx = find_file(filename);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC);
        return;
    }
    char* buf = file_table[idx].data;
    int pos = file_table[idx].size;
    int editing = 1;
    int maxlen = MAX_FILE_SIZE - 1;
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
        // If this is the last character, set cursor position
        if (i == pos - 1) {
            cursor_row = logical_row;
            cursor_col = logical_col;
        }
    }
    // Clear the rest of the edit area
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
        if (ctrl && scancode == 0x1F) { // Ctrl+S
            file_table[idx].size = pos;
            buf[pos] = 0;
            just_saved = 1;
            // Wait for ctrl release
            while (1) {
                unsigned char sc;
                asm volatile("inb $0x60, %0" : "=a"(sc));
                if (sc == 0x9D) break; // ctrl release
            }
            break;
        }
        if (ctrl && scancode == 0x10) { // Ctrl+Q
            print_string("[Quit]", -1, video, cursor, 0x0C);
            break;
        }
        if (scancode == 0x1C && pos < maxlen) { // Enter
            buf[pos++] = '\n';
        }
        else if (scancode == 0x0E && pos > 0) { // Backspace
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
        // Redraw buffer after every change
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
        *cursor = draw_cursor;
        unsigned short pos_hw = *cursor;
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos_hw & 0xFF)), "Nd"((unsigned short)0x3D5));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos_hw >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
    }
    // Clear screen
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }
    *cursor = 0;
}

//CALCULATOR PARSER

//forward declarations
static int parse_expr(const char **s);
static int parse_term(const char **s);
static int parse_factor(const char **s);
static int parse_number(const char **s);
static void skip_spaces(const char **s);

static void skip_spaces(const char **s) {
    while (**s == ' ') (*s)++;
}

static int parse_number(const char **s) {
    skip_spaces(s);
    int val = 0;
    int neg = 0;
    if (**s == '-') {
        neg = 1;
        (*s)++;
    }
    while (**s >= '0' && **s <= '9') {
        val = val * 10 + (**s - '0');
        (*s)++;
    }
    return neg ? -val : val;
}

static int parse_factor(const char **s) {
    skip_spaces(s);
    int val;
    if (**s == '(') {
        (*s)++;
        val = parse_expr(s);
        skip_spaces(s);
        if (**s == ')') (*s)++;
        else ; // 
        return val;
    } else {
        return parse_number(s);
    }
}

static int parse_power(const char **s) {
    int base = parse_factor(s);
    skip_spaces(s);
    while (**s == '^') {
        (*s)++;
        int exp = parse_power(s); //
        int result = 1;
        for (int i = 0; i < exp; i++) result *= base;
        base = result;
        skip_spaces(s);
    }
    return base;
}

static int parse_term(const char **s) {
    int val = parse_power(s);
    skip_spaces(s);
    while (**s == '*' || **s == '/') {
        char op = **s;
        (*s)++;
        int rhs = parse_power(s);
        if (op == '*') val *= rhs;
        else if (op == '/') val /= rhs;
        skip_spaces(s);
    }
    return val;
}

static int parse_expr(const char **s) {
    int val = parse_term(s);
    skip_spaces(s);
    while (**s == '+' || **s == '-') {
        char op = **s;
        (*s)++;
        int rhs = parse_term(s);
        if (op == '+') val += rhs;
        else if (op == '-') val -= rhs;
        skip_spaces(s);
    }
    return val;
}

// tells if a string is a math expression or not
static int is_math_expr(const char* s) {
    skip_spaces(&s);
    if ((*s >= '0' && *s <= '9') || *s == '(' || *s == '-') return 1;
    return 0;
}

static void handle_calc_command(const char* expr, char* video, int* cursor) {
    const char* s = expr;
    int result = parse_expr(&s);
    char buf[32];
    int n = 0, r = result;
    int neg = 0;
    if (r < 0) { neg = 1; r = -r; }
    do {
        buf[n++] = '0' + (r % 10);
        r /= 10;
    } while (r > 0 && n < 30);
    if (neg) buf[n++] = '-';
    for (int i = 0; i < n/2; i++) {
        char t = buf[i]; buf[i] = buf[n-1-i]; buf[n-1-i] = t;
    }
    buf[n] = 0;
    print_string(buf, n, video, cursor, 0x0A); // green
}



// History tracking
static char history[10][64]; // Store up to 10 commands, each up to 64 characters
static int history_count = 0;

// Add a command to history
static void add_to_history(const char* cmd) {
    if (history_count < 10) {
        int i = 0;
        while (cmd[i] && i < 63) {
            history[history_count][i] = cmd[i];
            i++;
        }
        history[history_count][i] = 0;
        history_count++;
    } else {
        // Shift history up to make room for the new command
        for (int i = 1; i < 10; i++) {
            for (int j = 0; j < 64; j++) {
                history[i - 1][j] = history[i][j];
            }
        }
        int i = 0;
        while (cmd[i] && i < 63) {
            history[9][i] = cmd[i];
            i++;
        }
        history[9][i] = 0;
    }
}

// Find file index by name, or -1 if not found
// Find file index by name in current directory, or -1 if not found
static int find_file(const char* name) {
    int nstart = 0;
    while (name[nstart] == ' ') nstart++;
    for (int i = 0; i < file_count; i++) {
        if (file_table[i].dir != current_dir) continue;
        int j = 0;
        while (name[nstart + j] && file_table[i].name[j] && name[nstart + j] == file_table[i].name[j]) j++;
        if ((!name[nstart + j] || name[nstart + j] == ' ') && !file_table[i].name[j]) return i;
    }
    return -1;
}

//ls
static void handle_ls_command(char* video, int* cursor, unsigned char color_unused) {
    int found = 0;
    //folders in current directory
    for (int i = 0; i < MAX_DIRS; i++) {
        if (i == 0) continue; // skip root dir itself
        if (dir_table[i].used && dir_table[i].parent == current_dir) {
            print_string(dir_table[i].name, -1, video, cursor, 0xB); 
            print_string_sameline("/", 1, video, cursor, 0xB);
            found = 1;
        }
    }
    //files in current directory
    for (int i = 0; i < file_count; i++) {
        if (file_table[i].dir == current_dir) {
            print_string(file_table[i].name, -1, video, cursor, 0xB); 
            found = 1;
        }
    }
    if (!found) print_string("(empty)", 7, video, cursor, 0xB);
}

// Debug: list all directory slots
static void handle_lsall_command(char* video, int* cursor) {
    char buf[128];
    for (int i = 0; i < MAX_DIRS; i++) {
        int n = 0;
        buf[n++] = '#';
        if (i >= 10) buf[n++] = '0' + (i / 10);
        buf[n++] = '0' + (i % 10);
        buf[n++] = ':';
        buf[n++] = ' ';
        int j = 0;
        while (dir_table[i].name[j] && n < 40) buf[n++] = dir_table[i].name[j++];
        buf[n++] = ' ';
        buf[n++] = '[';
        buf[n++] = dir_table[i].used ? 'U' : 'u';
        buf[n++] = ',';
        // parent as signed int
        int p = dir_table[i].parent;
        if (p < 0) { buf[n++] = '-'; p = -p; }
        if (p >= 10) buf[n++] = '0' + (p / 10);
        buf[n++] = '0' + (p % 10);
        buf[n++] = ']';
        buf[n++] = 0;
        print_string(buf, -1, video, cursor, 0xE);
    }
}

//cat file.txt
static void handle_cat_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = find_file(filename);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC); 
        return;
    }
    print_string(file_table[idx].data, file_table[idx].size, video, cursor, 0xB); 
}

//echo "asdf" > file.txt 
static void handle_echo_command(const char* text, const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = find_file(filename);
    if (idx == -1) {
        if (file_count >= MAX_FILES) {
            print_string("File table full", 15, video, cursor, 0xC); // error
            return;
        }
        idx = file_count++;
        int j = 0;
        while (filename[j] && j < MAX_FILE_NAME - 1) {
            file_table[idx].name[j] = filename[j];
            j++;
        }
        //txt extension by default
        if (j < MAX_FILE_NAME - 4 && !(filename[j - 4] == '.' && filename[j - 3] == 't' && filename[j - 2] == 'x' && filename[j - 1] == 't')) {
            file_table[idx].name[j++] = '.';
            file_table[idx].name[j++] = 't';
            file_table[idx].name[j++] = 'x';
            file_table[idx].name[j++] = 't';
        }
        file_table[idx].name[j] = 0;
        file_table[idx].dir = current_dir;
    }
    int len = 0;
    while (text[len] && len < MAX_FILE_SIZE - 1) {
        file_table[idx].data[len] = text[len];
        len++;
    }
    file_table[idx].data[len] = 0;
    file_table[idx].size = len;
    file_table[idx].dir = current_dir;
    print_string("OK", 2, video, cursor, 0xA); // confirmation
}

//rm file.txt
static void handle_rm_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = find_file(filename);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC); 
        return;
    }
    //index
    for (int i = idx; i < file_count - 1; i++) {
        for (int j = 0; j < MAX_FILE_NAME; j++) file_table[i].name[j] = file_table[i+1].name[j];
        for (int j = 0; j < MAX_FILE_SIZE; j++) file_table[i].data[j] = file_table[i+1].data[j];
        file_table[i].size = file_table[i+1].size;
    }
    file_count--;
    print_string("Deleted", 7, video, cursor, 0xA); 
}
static int mini_strcmp(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return a[i] - b[i];
        i++;
    }
    return a[i] - b[i];
}

static void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
static void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);

// read byte from cmos
static unsigned char cmos_read(unsigned char reg) {
    unsigned char value;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)reg), "Nd"((uint16_t)0x70));
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"((uint16_t)0x71));
    return value;
}

// Convert BCD to binary
static unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

//time (UTC)
static void get_time_string(char* buf) {
    unsigned char hour = cmos_read(0x04);
    unsigned char min  = cmos_read(0x02);
    unsigned char sec  = cmos_read(0x00);
    //convert from bcd
    hour = bcd_to_bin(hour);
    min  = bcd_to_bin(min);
    sec  = bcd_to_bin(sec);
    buf[0] = '0' + (hour / 10);
    buf[1] = '0' + (hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (min / 10);
    buf[4] = '0' + (min % 10);
    buf[5] = ':';
    buf[6] = '0' + (sec / 10);
    buf[7] = '0' + (sec % 10);
    buf[8] = 0;
}


static void handle_time_command(char* video, int* cursor, unsigned char color) {
    char timebuf[9];
    get_time_string(timebuf);
    print_string(timebuf, 8, video, cursor, color);
    print_string_sameline(" UTC", 4, video, cursor, color);
}

//clear
static void handle_clear_command(char* video, int* cursor) {
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    *cursor = 0;
}

//move rename file ~
static void handle_mv_command(const char* oldname, const char* newname, char* video, int* cursor) {
    int idx = find_file(oldname);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC);
        return;
    }
    int i = 0;
    while (newname[i] && i < MAX_FILE_NAME - 1) {
        file_table[idx].name[i] = newname[i];
        i++;
    }
    file_table[idx].name[i] = 0;
    print_string("File renamed", 12, video, cursor, 0xA);
}



//for basic call and reply commands
static void handle_command(const char* cmd, char* video, int* cursor, const char* input, const char* output, unsigned char color) {
    if (mini_strcmp(cmd, input) == 0) {
        print_string(output, -1, video, cursor, color);

    }
}

//main command dispatcher
static void handle_mkdir_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    // Check if subdir exists in current dir
    // Check if subdir exists in current dir
    for (int i = 0; i < MAX_DIRS; i++) {
        if (dir_table[i].used && dir_table[i].parent == current_dir && mini_strcmp(dir_table[i].name, dirname) == 0) {
            print_string("Directory exists", 16, video, cursor, 0xC); 
            return;
        }
    }
    int idx = -1;
    for (int i = 1; i < MAX_DIRS; i++) { // start at 1, never overwrite root
        if (!dir_table[i].used) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        print_string("Dir table full", 14, video, cursor, 0xC); 
        return;
    }
    int j = 0;
    while (dirname[j] && j < MAX_DIR_NAME-1) {
        dir_table[idx].name[j] = dirname[j];
        j++;
    }
    dir_table[idx].name[j] = 0;
    dir_table[idx].used = 1;
    dir_table[idx].parent = current_dir;
    if (idx >= dir_count) dir_count = idx + 1;
    print_string("Dir created", 11, video, cursor, 0xA); 
}

static void handle_cd_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    if (mini_strcmp(dirname, "..") == 0) {
        if (current_dir == 0) {
            print_string("Already at root", 15, video, cursor, 0xC); 
            return;
        }
        current_dir = dir_table[current_dir].parent;
        print_string("Changed dir", 11, video, cursor, 0xA); 
        return;
    }
    for (int i = 0; i < dir_count; i++) {
        if (dir_table[i].used && dir_table[i].parent == current_dir && mini_strcmp(dir_table[i].name, dirname) == 0) {
            current_dir = i;
            print_string("Changed dir", 11, video, cursor, 0xA); 
            return;
        }
    }
    print_string("No such dir", 11, video, cursor, 0xC); 
}

// Remove empty directory command
static void handle_rmdir_command(const char* dirname, char* video, int* cursor) {
    for (int i = 0; i < dir_count; i++) {
        if (dir_table[i].used && dir_table[i].parent == current_dir && mini_strcmp(dir_table[i].name, dirname) == 0) {
            // Check if directory is empty
            for (int j = 0; j < dir_count; j++) {
                if (dir_table[j].used && dir_table[j].parent == i) {
                    print_string("Directory not empty", 20, video, cursor, 0xC);
                    return;
                }
            }
            for (int j = 0; j < file_count; j++) {
                if (file_table[j].dir == i) {
                    print_string("Directory not empty", 20, video, cursor, 0xC);
                    return;
                }
            }
            dir_table[i].used = 0;
            print_string("Directory removed", 18, video, cursor, 0xA);
            return;
        }
    }
    print_string("No such directory", 18, video, cursor, 0xC);
}

//integer to string utility
static void int_to_str(int value, char* buf) {
    char temp[12];
    int i = 0, j = 0;
    int is_negative = value < 0;
    if (is_negative) value = -value;
    do {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    if (is_negative) temp[i++] = '-';
    while (i > 0) buf[j++] = temp[--i];
    buf[j] = 0;
}

//concat ultility
static void str_concat(char* dest, const char* src) {
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = 0;
}

//ram usage
static void handle_free_command(char* video, int* cursor) {
    char buf[64] = "Files: ";
    char temp[12];
    int_to_str(file_count, temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str(MAX_FILES, temp);
    str_concat(buf, temp);
    str_concat(buf, ", Dirs: ");
    int_to_str(dir_count, temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str(MAX_DIRS, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, 0xB);
}

// Filesystem usage summary command
static void handle_df_command(char* video, int* cursor) {
    char buf[64] = "Used files: ";
    char temp[12];
    int_to_str(file_count, temp);
    str_concat(buf, temp);
    str_concat(buf, ", Free: ");
    int_to_str(MAX_FILES - file_count, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, 0xB);
}

// Detailed version info command
static void handle_ver_command(char* video, int* cursor) {
    print_string("Smiggles OS v1.0.0\nDeveloped by Jules Miller and Vajra Vanukuri", -1, video, cursor, 0xD);
}

// System uptime command
static void handle_uptime_command(char* video, int* cursor) {
    static int ticks = 0; // Increment this in a timer interrupt handler
    char buf[64] = "Uptime: ";
    char temp[12];
    int_to_str(ticks / 18, temp); // Assuming 18.2 ticks per second
    str_concat(buf, temp);
    str_concat(buf, " seconds");
    print_string(buf, -1, video, cursor, 0xB);
}

// Halt command
static void handle_halt_command(char* video, int* cursor) {
    handle_clear_command(video, cursor);
    print_string("System halted", 13, video, cursor, 0xC);
    while (1) {}
}

// Reboot command
static void handle_reboot_command() {
    asm volatile ("int $0x19"); // BIOS reboot interrupt
}

// Custom function to convert a byte to a hexadecimal string
static void byte_to_hex(unsigned char byte, char* buf) {
    const char hex_chars[] = "0123456789ABCDEF";
    buf[0] = hex_chars[(byte >> 4) & 0xF];
    buf[1] = hex_chars[byte & 0xF];
    buf[2] = ' ';
    buf[3] = 0;
}

// Hexdump command
static void handle_hexdump_command(const char* filename, char* video, int* cursor) {
    int idx = find_file(filename);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC);
        return;
    }
    char buf[4];
    for (int i = 0; i < file_table[idx].size; i++) {
        byte_to_hex((unsigned char)file_table[idx].data[i], buf);
        print_string(buf, -1, video, cursor, 0xB);
    }
}

// History command
static void handle_history_command(char* video, int* cursor) {
    for (int i = 0; i < history_count; i++) {
        print_string(history[i], -1, video, cursor, 0xB);
    }
}

// Dispatch command
static void dispatch_command(const char* cmd, char* video, int* cursor) {
        // nano-like editor: edit filename.txt
        if (cmd[0] == 'e' && cmd[1] == 'd' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == ' ') {
            int start = 5;
            while (cmd[start] == ' ') start++;
            char filename[MAX_FILE_NAME];
            int fn = 0;
            while (cmd[start] && fn < MAX_FILE_NAME-1) filename[fn++] = cmd[start++];
            filename[fn] = 0;
            nano_editor(filename, video, cursor);
            return;
        }
    add_to_history(cmd); // Add the command to history
    if (mini_strcmp(cmd, "ping") == 0) {
        handle_command(cmd, video, cursor, "ping", "pong", 0xA); // confirmation
    } else if (mini_strcmp(cmd, "about") == 0) {
        handle_command(cmd, video, cursor, "about", "Smiggles v1.0.0\nJules Miller and Vajra Vanukuri", 0xD); // help/about
    } else if (mini_strcmp(cmd, "help") == 0) {
        handle_command(cmd, video, cursor, "help", "Available commands:\nprint \"text\" (prints text)\necho \"text\" > file.txt (creates file)\nls (view all files)\ncat file.txt (read contents of file)\nrm file.txt (delete file)\nmkdir dirname (make dir)\ncd dirname (change dir)\nedit file.txt (nano editor)\ntime (displays time in UTC)\nclear/cls (clear screen)\nmv oldname newname (rename/move file)\nrmdir dirname (remove empty dir)\nfree (RAM/file table usage)\ndf (filesystem usage)\nver (version info)\nuptime (system uptime)\nhalt (shutdown)\nreboot (restart)\nhexdump file.txt (hex view of file)\nhistory (recent commands)", 0xD); // help/about
    } else if (is_math_expr(cmd)) {
        handle_calc_command(cmd, video, cursor);
    } else if (mini_strcmp(cmd, "lsall") == 0) {
        handle_lsall_command(video, cursor);
    } else if (cmd[0] == 'm' && cmd[1] == 'k' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && cmd[5] == ' ') {
        int start = 6;
        while (cmd[start] == ' ') start++;
        char dirname[MAX_DIR_NAME];
        int dn = 0;
        while (cmd[start] && dn < MAX_DIR_NAME-1) {
            dirname[dn++] = cmd[start++];
        }
        dirname[dn] = 0;
        handle_mkdir_command(dirname, video, cursor, 0x0B);
    } else if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
        int start = 3;
        while (cmd[start] == ' ') start++;
        char dirname[MAX_DIR_NAME];
        int dn = 0;
        while (cmd[start] && dn < MAX_DIR_NAME-1) {
            dirname[dn++] = cmd[start++];
        }
        dirname[dn] = 0;
        handle_cd_command(dirname, video, cursor, 0x0B);
    } else if (mini_strcmp(cmd, "time") == 0) {
        handle_time_command(video, cursor, 0x0A);
    } else if (mini_strcmp(cmd, "ls") == 0) {
        handle_ls_command(video, cursor, 0x0B);
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
        //handling stupid spaces
        int start = 4;
        while (cmd[start] == ' ') start++;
        char filename[MAX_FILE_NAME];
        int fn = 0;
        while (cmd[start]) {
            if (cmd[start] != ' ' && fn < MAX_FILE_NAME-1) filename[fn++] = cmd[start];
            start++;
        }
        filename[fn] = 0;
        handle_cat_command(filename, video, cursor, 0x0E);
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ') {
        // rm file.txt
        int start = 3;
        while (cmd[start] == ' ') start++;
        char filename[MAX_FILE_NAME];
        int fn = 0;
        while (cmd[start]) {
            if (cmd[start] != ' ' && fn < MAX_FILE_NAME-1) filename[fn++] = cmd[start];
            start++;
        }
        filename[fn] = 0;
        handle_rm_command(filename, video, cursor, 0x0C);
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == ' ') {
        //format: echo "text" > filename
        int quote_start = 5;
        while (cmd[quote_start] == ' ') quote_start++;
        if (cmd[quote_start] != '"') {
            print_string("Bad echo syntax", 16, video, cursor, 0x0C);
            return;
        }
        int quote_end = quote_start + 1;
        while (cmd[quote_end] && cmd[quote_end] != '"') quote_end++;
        if (cmd[quote_end] != '"') {
            print_string("Bad echo syntax", 16, video, cursor, 0x0C);
            return;
        }
        
        int gt = quote_end + 1;
        while (cmd[gt] && cmd[gt] != '>') gt++;
        if (cmd[gt] != '>') {
            print_string("Bad echo syntax", 16, video, cursor, 0x0C);
            return;
        }
        int text_len = quote_end - (quote_start + 1);
        char text[MAX_FILE_SIZE];
        for (int i = 0; i < text_len && i < MAX_FILE_SIZE-1; i++) text[i] = cmd[quote_start + 1 + i];
        text[text_len] = 0;
        //strip spaces
        char filename[MAX_FILE_NAME];
        int fn = 0;
        int fi = gt + 1;
        while (cmd[fi]) {
            if (cmd[fi] != ' ' && fn < MAX_FILE_NAME-1) filename[fn++] = cmd[fi];
            fi++;
        }
        filename[fn] = 0;
        handle_echo_command(text, filename, video, cursor, 0x0A);
    } else if (mini_strcmp(cmd, "clear") == 0 || mini_strcmp(cmd, "cls") == 0) {
        handle_clear_command(video, cursor);
    } else if (cmd[0] == 'm' && cmd[1] == 'v' && cmd[2] == ' ') {
        int start = 3;
        while (cmd[start] == ' ') start++;
        char oldname[MAX_FILE_NAME], newname[MAX_FILE_NAME];
        int oi = 0, ni = 0;
        while (cmd[start] && cmd[start] != ' ' && oi < MAX_FILE_NAME - 1) oldname[oi++] = cmd[start++];
        oldname[oi] = 0;
        while (cmd[start] == ' ') start++;
        while (cmd[start] && ni < MAX_FILE_NAME - 1) newname[ni++] = cmd[start++];
        newname[ni] = 0;
        handle_mv_command(oldname, newname, video, cursor);
    } else if (mini_strcmp(cmd, "rmdir") == 0) {
        handle_rmdir_command(cmd + 6, video, cursor);
    } else if (mini_strcmp(cmd, "free") == 0) {
        handle_free_command(video, cursor);
    } else if (mini_strcmp(cmd, "df") == 0) {
        handle_df_command(video, cursor);
    } else if (mini_strcmp(cmd, "ver") == 0) {
        handle_ver_command(video, cursor);
    } else if (mini_strcmp(cmd, "uptime") == 0) {
        handle_uptime_command(video, cursor);
    } else if (mini_strcmp(cmd, "halt") == 0) {
        handle_halt_command(video, cursor);
    } else if (mini_strcmp(cmd, "reboot") == 0) {
        handle_reboot_command();
    } else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'x' && cmd[3] == 'd' && cmd[4] == 'u' && cmd[5] == 'm' && cmd[6] == 'p') {
        handle_hexdump_command(cmd + 8, video, cursor);
    } else if (mini_strcmp(cmd, "history") == 0) {
        handle_history_command(video, cursor);
    }
}

//print a string on NEW LINE with color
static void print_string(const char* str, int len, char* video, int* cursor, unsigned char color) {
    *cursor = ((*cursor / 80) + 1) * 80; //this is what goes to the new line
    // If len < 0, auto-calculate string length
    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }
    for (int i = 0; i < len; ) {
        // Handle "\\n" (two-character sequence)
        if (str[i] == '\\' && (i+1 < len) && str[i+1] == 'n') {
            *cursor = ((*cursor / 80) + 1) * 80;
            if (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i += 2;
            continue;
        }
        // Handle actual newline character (char 10)
        if (str[i] == '\n' || str[i] == 10) {
            *cursor = ((*cursor / 80) + 1) * 80;
            if (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i++;
            continue;
        }
        if (*cursor >= 80*25) {
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
static void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color) {
    // If len < 0, auto-calculate string length
    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }
    for (int i = 0; i < len && *cursor < 80*25 - 1; ) {
        // Handle "\\n" (two-character sequence)
        if (str[i] == '\\' && (i+1 < len) && str[i+1] == 'n') {
            *cursor = ((*cursor / 80) + 1) * 80;
            i += 2;
            continue;
        }
        // Handle actual newline character (char 10)
        if (str[i] == '\n' || str[i] == 10) {
            *cursor = ((*cursor / 80) + 1) * 80;
            i++;
            continue;
        }
        video[(*cursor)*2] = str[i];
        video[(*cursor)*2+1] = color;
        (*cursor)++;
        i++;
    }
}

void kernel_main(void) {
    // --- Interrupt setup ---
    pic_remap();
    set_idt_entry(0x20, (unsigned int)irq0_timer_handler);
    set_idt_entry(0x21, (unsigned int)irq1_keyboard_handler);
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
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((uint16_t)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((uint16_t)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));

    
    char cmd_buf[64];
    int cmd_len = 0;
    int cmd_cursor = 0; // position within the input line

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

        if (scancode > 0x80) {
            int e0_prefix = 0;
            while (1) {
                unsigned char scancode;
                asm volatile("inb $0x60, %0" : "=a"(scancode));

                // Handle E0 prefix for arrow keys
                if (scancode == 0xE0) {
                    e0_prefix = 1;
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

                if (scancode > 0x80) {
                    prev_scancode = 0;
                    e0_prefix = 0;
                }
                else if (scancode != prev_scancode && scancode != 0) {
                    prev_scancode = scancode;

                    // Handle left/right arrow keys (E0 4B and E0 4D)
                    if (e0_prefix) {
                        if (scancode == 0x4B) { // Left arrow
                            if (cmd_cursor > 0) {
                                cmd_cursor--;
                                cursor--;
                                // Move hardware cursor
                                unsigned short pos = cursor;
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                            }
                            e0_prefix = 0;
                            continue;
                        } else if (scancode == 0x4D) { // Right arrow
                            if (cmd_cursor < cmd_len) {
                                cmd_cursor++;
                                cursor++;
                                // Move hardware cursor
                                unsigned short pos = cursor;
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                            }
                            e0_prefix = 0;
                            continue;
                        }
                        // Ignore other E0-prefixed keys for now
                        e0_prefix = 0;
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
                        [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ', [0x1C] = '\n', [0x0E] = 8, // backspace
                        [0x0F] = '\t',
                        //numpad keys
                        [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x4B] = '4', [0x4C] = '5', [0x4D] = '6', [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x52] = '0',
                        [0x53] = '.', [0x37] = '*', [0x4A] = '-', [0x4E] = '+', [0x35] = '/',
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
                        [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ', [0x1C] = '\n', [0x0E] = 8, // backspace
                        [0x0F] = '\t',

                        [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x4B] = '4', [0x4C] = '5', [0x4D] = '6', [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x52] = '0',
                        [0x53] = '.', [0x37] = '*', [0x4A] = '-', [0x4E] = '+', [0x35] = '/',
                    };

                    if (shift)
                        c = upper_table[scancode];
                    else
                        c = lower_table[scancode];

                    if (c) {
                        if (c == '\n') {
                            cmd_buf[cmd_len] = 0;
                            // Print command: print "text"
                            if (cmd_buf[0] == 'p' && cmd_buf[1] == 'r' && cmd_buf[2] == 'i' && cmd_buf[3] == 'n' && cmd_buf[4] == 't' && cmd_buf[5] == ' ' && cmd_buf[6] == '"') {
                                // Find closing quote
                                int start = 7;
                                int end = start;
                                while (cmd_buf[end] && cmd_buf[end] != '"') end++;
                                if (cmd_buf[end] == '"') {
                                    // Print the string between quotes
                                    // Move cursor to new line before printing
                                    print_string(&cmd_buf[start], end - start, video, &cursor, 0x0D);
                                }
                            } else {
                                dispatch_command(cmd_buf, video, &cursor);
                            }
                            if (just_saved) {
                                just_saved = 0;
                                print_smiggles_art(video, &cursor);
                                cursor += 80; // add one line space
                                cursor = ((cursor / 80) + 1) * 80;
                                if (cursor >= 80*25) {
                                    scroll_screen(video);
                                    cursor -= 80;
                                }
                                print_string("Saved", 5, video, &cursor, 0x0A);
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
                            // Update hardware cursor
                            unsigned short pos_hw = cursor;
                            asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                            asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos_hw & 0xFF)), "Nd"((unsigned short)0x3D5));
                            asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                            asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos_hw >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                            line_start = cursor;
                            cmd_len = 0;
                            cmd_cursor = 0;
                        }
                        else if (c == 8) {
                            if (cmd_cursor > 0 && cmd_len > 0 && cursor > line_start) {
                                // Shift buffer left from cursor
                                for (int k = cmd_cursor-1; k < cmd_len-1; k++)
                                    cmd_buf[k] = cmd_buf[k+1];
                                cmd_len--;
                                cmd_cursor--;
                                cursor--;
                                // Redraw input line
                                int redraw = cursor;
                                for (int k = 0; k < cmd_len-cmd_cursor; k++) {
                                    video[(redraw+k)*2] = cmd_buf[cmd_cursor+k];
                                    video[(redraw+k)*2+1] = 0x0F;
                                }
                                // Clear last char
                                video[(line_start+cmd_len)*2] = ' ';
                                video[(line_start+cmd_len)*2+1] = 0x07;
                                // Move hardware cursor
                                unsigned short pos = cursor;
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                                asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                            }
                        }
                        else if (c == '\t' && cursor < 80*25 - 4) {
                            for (int t = 0; t < 4; t++) {
                                video[cursor*2] = ' ';
                                video[cursor*2+1] = 0x0F;
                                cursor++;
                            }
                        }
                        else {
                            // Allow input to flow to next line
                            if (cursor % 80 == 0 && cursor != line_start && cursor < 80*25) {
                                if (cursor >= 80*25) {
                                    scroll_screen(video);
                                    cursor -= 80;
                                }
                            }
                            if (cursor < 80*25 - 1 && c != '\t') {
                                if (cmd_len < 63) {
                                    // Insert at cursor position
                                    for (int k = cmd_len; k > cmd_cursor; k--)
                                        cmd_buf[k] = cmd_buf[k-1];
                                    cmd_buf[cmd_cursor] = c;
                                    cmd_len++;
                                    // Redraw input line
                                    int redraw = cursor;
                                    for (int k = 0; k < cmd_len-cmd_cursor; k++) {
                                        video[(redraw+k)*2] = cmd_buf[cmd_cursor+k];
                                        video[(redraw+k)*2+1] = 0x0F;
                                    }
                                    cursor++;
                                    cmd_cursor++;
                                    // Move hardware cursor
                                    unsigned short pos = cursor;
                                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
                                    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
                                    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
                                    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
                                }
                            }
                        }
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
