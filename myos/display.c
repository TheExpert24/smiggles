#include "kernel.h"

// --- Display Functions ---

void scroll_screen(char* video) {
    //move all lines up by one
    for (int row = 1; row < 25; row++) {
        for (int col = 0; col < 80; col++) {
            video[((row-1)*80+col)*2] = video[(row*80+col)*2];
            video[((row-1)*80+col)*2+1] = video[(row*80+col)*2+1];
        }
    }
    // clear last line
    for (int col = 0; col < 80; col++) {
        video[((24)*80+col)*2] = ' ';
        video[((24)*80+col)*2+1] = 0x07;
    }
}

void print_smiggles_art(char* video, int* cursor) {
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

//print a string on NEW LINE with color
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color) {
    *cursor = ((*cursor / 80) + 1) * 80; //this is what goes to the new line
    while (*cursor >= 80*25) {
        scroll_screen(video);
        *cursor -= 80;
    }
    // If len < 0, auto-calculate string length
    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }
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
    // If len < 0, auto-calculate string length
    if (len < 0) {
        len = 0;
        while (str[len]) len++;
    }
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
    while (1) {
        if (keyboard_pop_scancode(&scancode)) {
            // Handle shift
            if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
            if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }
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
    [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ', [0x1C] = '\n', [0x0E] = 8,
    [0x0F] = '\t',
    [0x37] = '*', [0x4A] = '-', [0x4E] = '+',
};

// Set VGA hardware cursor position
void set_cursor_position(int cursor) {
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

// Convert scancode to character based on shift state
char scancode_to_char(unsigned char scancode, int shift) {
    // Numpad keys
    switch (scancode) {
        case 0x52: return '0';
        case 0x4F: return '1';
        case 0x50: return '2';
        case 0x51: return '3';
        case 0x4B: return '4';
        case 0x4C: return '5';
        case 0x4D: return '6';
        case 0x47: return '7';
        case 0x48: return '8';
        case 0x49: return '9';
        case 0x53: return '.';
        case 0x37: return '*';
        case 0x4A: return '-';
        case 0x4E: return '+';
        case 0x35: return '/';
        case 0x1C: return '\n'; // Enter
    }
    // Default: use tables
    if (shift) {
        return upper_table[scancode];
    } else {
        return lower_table[scancode];
    }
}