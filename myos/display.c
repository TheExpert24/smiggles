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
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color) {
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