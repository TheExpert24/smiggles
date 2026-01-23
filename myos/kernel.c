static int mini_strcmp(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return a[i] - b[i];
        i++;
    }
    return a[i] - b[i];
}

static void handle_command(const char* cmd, char* video, int* cursor, const char* input, const char* output, unsigned char color) {
    if (mini_strcmp(cmd, input) == 0) {
        *cursor = ((*cursor / 80) + 1) * 80;
        const char* out = output;
        int k = 0;
        while (out[k] && *cursor < 80*25 - 1) {
            video[(*cursor)*2] = out[k];
            video[(*cursor)*2+1] = color;
            (*cursor)++;
            k++;
        }
    }
}

void kernel_main(void) {
    char* video = (char*)0xB8000;
    int cursor = 0;
    int prompt_end = 0;
    int line_start = 0;
    unsigned char prev_scancode = 0;
    int shift = 0;

    //clear screen
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }

    //introductory message
    const char* smiggles_art[8] = {
        " _______  __   __  ___   _______  _______  ___      _______  _______ ",
        "|       ||  |_|  ||   | |       ||       ||   |    |       ||       |",
        "|  _____||       ||   | |    ___||    ___||   |    |    ___||  _____|",
        "| |_____ |       ||   | |   | __ |   | __ |   |    |   |___ | |_____ ",
        "|_____  ||       ||   | |   ||  ||   ||  ||   |___ |    ___||_____  |",
        " _____| || ||_|| ||   | |   |_| ||   |_| ||       ||   |___  _____| |",
        "|_______||_|   |_||___| |_______||_______||_______||_______||_______|"
    
    };
    //yellow
    unsigned char rainbow[8] = {0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E};
    int art_lines = 8;
    for (int l = 0; l < art_lines; l++) {
        for (int j = 0; smiggles_art[l][j] && j < 80; j++) {
            video[(l*80+j)*2] = smiggles_art[l][j];
            video[(l*80+j)*2+1] = rainbow[j % 7];
        }
    }

    //prompt
    cursor = art_lines * 80;
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

    //cursor stuff
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));

    // Command buffer
    char cmd_buf[64];
    int cmd_len = 0;

    while (1) {
        unsigned char scancode;
        asm volatile("inb $0x60, %0" : "=a"(scancode));

        //SHIFT KE
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
        }
        else if (scancode != prev_scancode && scancode != 0) {
            prev_scancode = scancode;

            char c = 0;
            const char lower_table[128] = {
                [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
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
                    // Null-terminate and check command
                    cmd_buf[cmd_len] = 0;
                    handle_command(cmd_buf, video, &cursor, "ping", "pong", 0x0A);
                    handle_command(cmd_buf, video, &cursor, "help", "Lock in brutha", 0x0A);
                    handle_command(cmd_buf, video, &cursor, "about", "Smiggles v1.0.0 \n Jules Miller and Vajra Vanukuri", 0x0A);
                    // New prompt
                    cursor = ((cursor / 80) + 1) * 80;
                    const char* prompt = "> ";
                    int pi = 0;
                    while (prompt[pi] && cursor < 80*25 - 1) {
                        video[cursor*2] = prompt[pi];
                        video[cursor*2+1] = 0x0F;
                        cursor++;
                        pi++;
                    }
                    line_start = cursor;
                    cmd_len = 0;
                }
                else if (c == 8) {
                    if (cursor > line_start && cmd_len > 0) {
                        cursor--;
                        video[cursor*2] = ' ';
                        video[cursor*2+1] = 0x07;
                        cmd_len--;
                    }
                }
                else if (c == '\t' && cursor < 80*25 - 4) {
                    for (int t = 0; t < 4; t++) {
                        video[cursor*2] = ' ';
                        video[cursor*2+1] = 0x0F;
                        cursor++;
                    }
                }
                else if (cursor < 80*25 - 1 && c != '\t') {
                    if (cmd_len < 63) {
                        video[cursor*2] = c;
                        video[cursor*2+1] = 0x0F;
                        cursor++;
                        cmd_buf[cmd_len++] = c;
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


