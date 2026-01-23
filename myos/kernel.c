// Scroll the screen up by one line
static void scroll_screen(char* video) {
    // Move all lines up by one
    for (int row = 1; row < 25; row++) {
        for (int col = 0; col < 80; col++) {
            video[((row-1)*80+col)*2] = video[(row*80+col)*2];
            video[((row-1)*80+col)*2+1] = video[(row*80+col)*2+1];
        }
    }
    // Clear the last line
    for (int col = 0; col < 80; col++) {
        video[((24)*80+col)*2] = ' ';
        video[((24)*80+col)*2+1] = 0x07;
    }
}
// --- RAM-based file system ---

#define MAX_FILES 8
#define MAX_FILE_NAME 16
#define MAX_FILE_SIZE 128
#define MAX_DIRS 4
#define MAX_DIR_NAME 16

typedef struct {
    char name[MAX_DIR_NAME];
    int used;
    int parent; // index of parent directory, -1 for root
} RamDir;

static RamDir dir_table[MAX_DIRS] = { {"root", 1, -1} };
static int dir_count = 1;
static int current_dir = 0; // index in dir_table

// Forward declaration for print_string and print_string_sameline
static void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
static void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);



typedef struct {
    char name[MAX_FILE_NAME];
    char data[MAX_FILE_SIZE];
    int size;
    int dir; // directory index
} RamFile;

static RamFile file_table[MAX_FILES];
static int file_count = 0;

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

// List files (ls command)
static void handle_ls_command(char* video, int* cursor, unsigned char color_unused) {
    int found = 0;
    // List subdirectories in current directory
    for (int i = 0; i < dir_count; i++) {
        if (dir_table[i].used && dir_table[i].parent == current_dir && i != current_dir) {
            print_string(dir_table[i].name, -1, video, cursor, 0xB); // light cyan
            print_string_sameline("/", 1, video, cursor, 0xB);
            found = 1;
        }
    }
    // List files in current directory
    for (int i = 0; i < file_count; i++) {
        if (file_table[i].dir == current_dir) {
            print_string(file_table[i].name, -1, video, cursor, 0xB); // light cyan
            found = 1;
        }
    }
    if (!found) print_string("(empty)", 7, video, cursor, 0xB);
}

// Show file contents (cat command)
static void handle_cat_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = find_file(filename);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC); // error
        return;
    }
    print_string(file_table[idx].data, file_table[idx].size, video, cursor, 0xB); // file content: light cyan
}

// Write to file (echo command)
static void handle_echo_command(const char* text, const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = find_file(filename);
    if (idx == -1) {
        if (file_count >= MAX_FILES) {
            print_string("File table full", 15, video, cursor, 0xC); // error
            return;
        }
        idx = file_count++;
        int j = 0;
        while (filename[j] && j < MAX_FILE_NAME-1) {
            file_table[idx].name[j] = filename[j];
            j++;
        }
        file_table[idx].name[j] = 0;
        file_table[idx].dir = current_dir;
    }
    int len = 0;
    while (text[len] && len < MAX_FILE_SIZE-1) {
        file_table[idx].data[len] = text[len];
        len++;
    }
    file_table[idx].data[len] = 0;
    file_table[idx].size = len;
    file_table[idx].dir = current_dir;
    print_string("OK", 2, video, cursor, 0xA); // confirmation
}

// Delete a file (rm command)
static void handle_rm_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = find_file(filename);
    if (idx == -1) {
        print_string("File not found", 14, video, cursor, 0xC); // error
        return;
    }
    // Shift all files after idx left by one
    for (int i = idx; i < file_count - 1; i++) {
        for (int j = 0; j < MAX_FILE_NAME; j++) file_table[i].name[j] = file_table[i+1].name[j];
        for (int j = 0; j < MAX_FILE_SIZE; j++) file_table[i].data[j] = file_table[i+1].data[j];
        file_table[i].size = file_table[i+1].size;
    }
    file_count--;
    print_string("Deleted", 7, video, cursor, 0xA); // confirmation
}
static int mini_strcmp(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return a[i] - b[i];
        i++;
    }
    return a[i] - b[i];
}

// Forward declaration for print_string
static void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
static void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);

// Read a byte from CMOS register
static unsigned char cmos_read(unsigned char reg) {
    unsigned char value;
    asm volatile ("outb %0, %1" : : "a"(reg), "Nd"((unsigned short)0x70));
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"((unsigned short)0x71));
    return value;
}

// Convert BCD to binary
static unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// Get time string from CMOS (format: HH:MM:SS) and write to buf (must be at least 9 bytes)
static void get_time_string(char* buf) {
    unsigned char hour = cmos_read(0x04);
    unsigned char min  = cmos_read(0x02);
    unsigned char sec  = cmos_read(0x00);
    // Convert from BCD if needed (most BIOSes use BCD)
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

// Handle the 'time' command
static void handle_time_command(char* video, int* cursor, unsigned char color) {
    char timebuf[9];
    get_time_string(timebuf);
    print_string(timebuf, 8, video, cursor, color);
    print_string_sameline(" UTC", 4, video, cursor, color);
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
    for (int i = 0; i < dir_count; i++) {
        if (dir_table[i].used && dir_table[i].parent == current_dir && mini_strcmp(dir_table[i].name, dirname) == 0) {
            print_string("Directory exists", 16, video, cursor, 0xC); // error
            return;
        }
    }
    if (dir_count >= MAX_DIRS) {
        print_string("Dir table full", 14, video, cursor, 0xC); // error
        return;
    }
    int idx = dir_count++;
    int j = 0;
    while (dirname[j] && j < MAX_DIR_NAME-1) {
        dir_table[idx].name[j] = dirname[j];
        j++;
    }
    dir_table[idx].name[j] = 0;
    dir_table[idx].used = 1;
    dir_table[idx].parent = current_dir;
    print_string("Dir created", 11, video, cursor, 0xA); // confirmation
}

static void handle_cd_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    if (mini_strcmp(dirname, "..") == 0) {
        if (current_dir == 0) {
            print_string("Already at root", 15, video, cursor, 0xC); // error
            return;
        }
        current_dir = dir_table[current_dir].parent;
        print_string("Changed dir", 11, video, cursor, 0xA); // confirmation
        return;
    }
    for (int i = 0; i < dir_count; i++) {
        if (dir_table[i].used && dir_table[i].parent == current_dir && mini_strcmp(dir_table[i].name, dirname) == 0) {
            current_dir = i;
            print_string("Changed dir", 11, video, cursor, 0xA); // confirmation
            return;
        }
    }
    print_string("No such dir", 11, video, cursor, 0xC); // error
}

static void dispatch_command(const char* cmd, char* video, int* cursor) {
    if (mini_strcmp(cmd, "ping") == 0) {
        handle_command(cmd, video, cursor, "ping", "pong", 0xA); // confirmation
    } else if (mini_strcmp(cmd, "about") == 0) {
        handle_command(cmd, video, cursor, "about", "Smiggles v1.0.0\nJules Miller and Vajra Vanukuri", 0xD); // help/about
    } else if (mini_strcmp(cmd, "help") == 0) {
        handle_command(cmd, video, cursor, "help", "Available commands:\nprint \"text\" (prints text)  \necho \"text\" > file.txt (creates file)  \nls (view all files)\ncat file.txt (read contents of file)\nrm file.txt (delete file)\nmkdir dirname (make dir)\ncd dirname (change dir)\ntime (displays time in UTC)", 0xD); // help/about
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
    const char* smiggles_art[7] = {
        " _______  __   __  ___   _______  _______  ___      _______  _______ ",
        "|       ||  |_|  ||   | |       ||       ||   |    |       ||       |",
        "|  _____||       ||   | |    ___||    ___||   |    |    ___||  _____|",
        "| |_____ |       ||   | |   | __ |   | __ |   |    |   |___ | |_____ ",
        "|_____  ||       ||   | |   ||  ||   ||  ||   |___ |    ___||_____  |",
        " _____| || ||_|| ||   | |   |_| ||   |_| ||       ||   |___  _____| |",
        "|_______||_|   |_||___| |_______||_______||_______||_______||_______|"
    };
    //yellow
    unsigned char rainbow[7] = {0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E};
    int art_lines = 7;
    for (int l = 0; l < art_lines; l++) {
        
        for (int j = 0; j < 80; j++) {
            video[(l*80+j)*2] = ' ';
            video[(l*80+j)*2+1] = rainbow[l % 7];
        }
        
        for (int j = 0; smiggles_art[l][j] && j < 80; j++) {
            video[(l*80+j)*2] = smiggles_art[l][j];
            video[(l*80+j)*2+1] = rainbow[j % 7];
        }
    }

    
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

    
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));

    
    char cmd_buf[64];
    int cmd_len = 0;
    int cmd_cursor = 0; // position within the input line

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
