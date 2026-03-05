// ...existing code...
#include "kernel.h"

static void handle_filesize_command(const char* filename, char* video, int* cursor) {
    int node_idx = resolve_path(filename);
    if (node_idx == -1) {
        print_string("File not found", 14, video, cursor, COLOR_RED);
        return;
    }
    if (node_table[node_idx].type != NODE_FILE) {
        print_string("Not a file", 10, video, cursor, COLOR_RED);
        return;
    }
    char buf[64];
    str_copy(buf, "Size: ", 64);
    char temp[16];
    int_to_str(node_table[node_idx].content_size, temp);
    str_concat(buf, temp);
    str_concat(buf, " bytes");
    print_string(buf, -1, video, cursor, COLOR_LIGHT_CYAN);
}

// --- Global Variables ---
char history[10][64];
int history_count = 0;

static void print_file_already_exists_message(int node_idx, char* video, int* cursor) {
    char full_path[MAX_PATH_LENGTH];
    char location[MAX_PATH_LENGTH];
    char message[160];

    get_full_path(node_idx, full_path, MAX_PATH_LENGTH);

    int src = (full_path[0] == '/') ? 1 : 0;
    int dst = 0;
    while (full_path[src] && dst < MAX_PATH_LENGTH - 1) {
        location[dst++] = full_path[src++];
    }
    location[dst] = 0;

    message[0] = 0;
    str_concat(message, "File already exists");

    print_string(message, -1, video, cursor, COLOR_RED);
}

// --- Time Functions ---
unsigned char cmos_read(unsigned char reg) {
    unsigned char value;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)reg), "Nd"((uint16_t)0x70));
    asm volatile ("inb %1, %0" : "=a"(value) : "Nd"((uint16_t)0x71));
    return value;
}

unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void get_time_string(char* buf) {
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

// --- History Functions ---
void add_to_history(const char* cmd) {
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


// --- Neofetch ---
// --- Neofetch Command ---
static void handle_neofetch_command(char* video, int* cursor) {
    // Side-by-side logo and info
    const char* logo[] = {
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
        "              /      /",
    };

    int logo_lines = 12;
    int info_lines = 14;
    char uptime_buf[32];
    char temp[12];
    int seconds = ticks / 18;
    int minutes = seconds / 60;
    int hours = minutes / 60;
    seconds = seconds % 60;
    minutes = minutes % 60;
    uptime_buf[0] = 0;
    int_to_str(hours, temp); str_concat(uptime_buf, temp); str_concat(uptime_buf, "h ");
    int_to_str(minutes, temp); str_concat(uptime_buf, temp); str_concat(uptime_buf, "m ");
    int_to_str(seconds, temp); str_concat(uptime_buf, temp); str_concat(uptime_buf, "s");

    // --- Get CPU vendor string using CPUID ---
    char cpu_vendor[13];
    cpu_vendor[12] = 0;
    unsigned int eax, ebx, ecx, edx;
    eax = 0;
    __asm__ __volatile__ (
        "cpuid"
        : "=b"(ebx), "=d"(edx), "=c"(ecx)
        : "a"(eax)
    );
    ((unsigned int*)cpu_vendor)[0] = ebx;
    ((unsigned int*)cpu_vendor)[1] = edx;
    ((unsigned int*)cpu_vendor)[2] = ecx;

    char cpu_line[48];
    cpu_line[0] = 0;
    str_concat(cpu_line, "CPU: ");
    str_concat(cpu_line, cpu_vendor);

    // --- Use a simple hardcoded memory value ---
    char mem_line[48];
    mem_line[0] = 0;
    str_concat(mem_line, "Memory: 64 MiB (static)");

    const char* info[] = {
        "OS: Smiggles OS x86_64", // Could use macro if available
        "Host: QEMU 10.2.1",
        "Kernel: Smiggles OS v1.0.0", // Real version string
        "Uptime:", uptime_buf,
        "Packages: 0",
        "Shell: smigsh 0.1", // Real shell name
        "Resolution: 80x25", // Real resolution
        "Terminal: /dev/tty1", // Real terminal name
        cpu_line,
        "GPU: VGA Compatible Adapter",
        mem_line,
        "",
        "__COLORBAR__"
    };
    int max_lines = (logo_lines > info_lines) ? logo_lines : info_lines;
    for (int i = 0; i < max_lines; i++) {
        // Print logo part
        if (i < logo_lines && logo[i][0] != '\0') {
            print_string(logo[i], -1, video, cursor, 0x0A);
        } else {
            print_string("", -1, video, cursor, 0x0A);
        }
        // Pad to column 32 (wider gap)
        int pad = 32 - (i < logo_lines ? str_len(logo[i]) : 0);
        if (pad < 2) pad = 2; // always at least 2 spaces
        for (int j = 0; j < pad; j++) {
            print_string_sameline(" ", 1, video, cursor, 0x0A);
        }
        // Print info part with colored label
        if (i < info_lines && info[i][0] != '\0') {
            const char* line = info[i];
            if (str_equal(line, "__COLORBAR__")) {
                //colored block with ascii 219 █)
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_RED); // red
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_GREEN); // green
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_YELLOW); // yellow
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_BLUE); // blue
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_MAGENTA); // magenta
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_CYAN); // cyan
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_WHITE); // white
            } else if (str_equal(line, "Uptime:")) {
                // Print label (green)
                print_string_sameline("Uptime:", -1, video, cursor, COLOR_GREEN);
                print_string_sameline(" ", 1, video, cursor, COLOR_WHITE);
                print_string_sameline(info[i+1], -1, video, cursor, COLOR_WHITE);
                i++; // skip value line
            } else {
                // Find colon
                int colon = -1;
                for (int k = 0; line[k]; k++) {
                    if (line[k] == ':') { colon = k; break; }
                }
                if (colon > 0 && line[colon+1]) {
                    // Print label (green)
                    char label[24];
                    int l = 0;
                    for (; l <= colon && l < 23; l++) label[l] = line[l];
                    label[l] = 0;
                    print_string_sameline(label, -1, video, cursor, COLOR_GREEN); // green
                    // Print value (white)
                    print_string_sameline(" ", 1, video, cursor, COLOR_WHITE);
                    print_string_sameline(line+colon+1, -1, video, cursor, COLOR_WHITE); // white
                } else {
                    // No colon, print whole line in white
                    print_string_sameline(line, -1, video, cursor, COLOR_WHITE);
                }
            }
        }
    }

}



// --- Utility Functions ---

// find_file removed; use resolve_path and node_table for all file lookups

// --- Command Handlers ---
static void handle_command(const char* cmd, char* video, int* cursor, const char* input, const char* output, unsigned char color) {
    if (mini_strcmp(cmd, input) == 0) {
        print_string(output, -1, video, cursor, color);
    }
}

static void handle_ls_command(char* video, int* cursor, unsigned char color_unused) {
    FSNode* dir = &node_table[current_dir_idx];
    if (dir->child_count == 0) {
        print_string("(empty)", 7, video, cursor, COLOR_LIGHT_CYAN);
        return;
    }
    for (int i = 0; i < dir->child_count; i++) {
        int child_idx = dir->children_idx[i];
        FSNode* child = &node_table[child_idx];
        if (child->type == NODE_DIRECTORY) {
            print_string(child->name, -1, video, cursor, COLOR_LIGHT_CYAN);
            print_string_sameline("/", 1, video, cursor, COLOR_LIGHT_CYAN);
        } else {
            print_string(child->name, -1, video, cursor, COLOR_LIGHT_CYAN);
        }
    }
}

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
        int p = dir_table[i].parent;
        if (p < 0) { buf[n++] = '-'; p = -p; }
        if (p >= 10) buf[n++] = '0' + (p / 10);
        buf[n++] = '0' + (p % 10);
        buf[n++] = ']';
        buf[n++] = 0;
        print_string(buf, -1, video, cursor, COLOR_CYAN);
    }
}

static void handle_cat_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int node_idx = resolve_path(filename);
    if (node_idx == -1) {
        print_string("File not found", 14, video, cursor, COLOR_RED);
        return;
    }
    if (node_table[node_idx].type != NODE_FILE) {
        print_string("Not a file", 10, video, cursor, COLOR_RED);
        return;
    }
    if (node_table[node_idx].content_size <= 0) {
        print_string("File is empty", 13, video, cursor, COLOR_RED);
        return;
    }
    print_string(node_table[node_idx].content, node_table[node_idx].content_size, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_echo_command(const char* text, const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int node_idx = resolve_path(filename);
    if (node_idx != -1) {
        if (node_table[node_idx].type == NODE_FILE) {
            print_file_already_exists_message(node_idx, video, cursor);
        } else {
            print_string("Cannot write file", 17, video, cursor, COLOR_RED);
        }
        return;
    }

    node_idx = fs_touch(filename, text);
    if (node_idx >= 0 && node_table[node_idx].type == NODE_FILE) {
        int len = 0;
        while (text[len] && len < MAX_FILE_CONTENT - 1) {
            node_table[node_idx].content[len] = text[len];
            len++;
        }
        node_table[node_idx].content[len] = 0;
        node_table[node_idx].content_size = len;
        fs_save();
        print_string("OK", 2, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("Cannot write file", 17, video, cursor, COLOR_RED);
    }
}

static void handle_rm_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int result = fs_rm(filename, 0);
    if (result == -1) {
        print_string("File not found or cannot remove root", 37, video, cursor, COLOR_RED);
    } else if (result == -2) {
        print_string("Directory not empty. Use rmdir -r", 33, video, cursor, COLOR_RED);
    } else {
        print_string("Removed", 7, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_time_command(char* video, int* cursor, unsigned char color) {
    char timebuf[9];
    get_time_string(timebuf);
    print_string(timebuf, 8, video, cursor, color);
    print_string_sameline(" UTC", 4, video, cursor, color);
}

void handle_clear_command(char* video, int* cursor) {
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    *cursor = 0;
}

static void handle_mv_command(const char* oldname, const char* newname, char* video, int* cursor) {
    int src_idx = resolve_path(oldname);
    if (src_idx == -1) {
        print_string("Source not found", 16, video, cursor, COLOR_RED);
        return;
    }
    // Simple rename in same directory
    str_copy(node_table[src_idx].name, newname, MAX_NAME_LENGTH);
    fs_save();
    print_string("Renamed", 7, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_mkdir_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    int result = fs_mkdir(dirname);
    if (result == -1) {
        print_string("Parent directory not found", 26, video, cursor, COLOR_RED);
    } else if (result == -2) {
        print_string("Directory already exists", 24, video, cursor, COLOR_RED);
    } else if (result == -3) {
        print_string("No space for new directory", 26, video, cursor, COLOR_RED);
    } else {
        print_string("Directory created", 17, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_cd_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    if (str_equal(dirname, "")) {
        current_dir_idx = 0; // cd to root
        print_string("Changed to /", 12, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    int target_idx = resolve_path(dirname);
    if (target_idx == -1) {
        print_string("Directory not found", 19, video, cursor, COLOR_RED);
        return;
    }
    if (node_table[target_idx].type != NODE_DIRECTORY) {
        print_string("Not a directory", 15, video, cursor, COLOR_RED);
        return;
    }
    current_dir_idx = target_idx;
    char path[MAX_PATH_LENGTH];
    get_full_path(current_dir_idx, path, MAX_PATH_LENGTH);
    print_string("Changed to: ", -1, video, cursor, COLOR_LIGHT_GREEN);
    print_string_sameline(path, -1, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_rmdir_command(const char* dirname, char* video, int* cursor) {
    int is_recursive = 0;
    const char* path = dirname;
    // Check for -r flag
    if (dirname[0] == '-' && dirname[1] == 'r' && dirname[2] == ' ') {
        is_recursive = 1;
        path = dirname + 3;
        while (*path == ' ') path++;
    }
    int result = fs_rm(path, is_recursive);
    if (result == -1) {
        print_string("Directory not found", 19, video, cursor, COLOR_RED);
    } else if (result == -2) {
        print_string("Directory not empty. Use -r flag", 32, video, cursor, COLOR_RED);
    } else {
        print_string("Directory removed", 17, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_free_command(char* video, int* cursor) {
    // Count files and directories from node_table
    int file_count_actual = 0;
    int dir_count_actual = 0;
    
    for (int i = 0; i < MAX_NODES; i++) {
        if (node_table[i].used) {
            if (node_table[i].type == NODE_FILE) {
                file_count_actual++;
            } else if (node_table[i].type == NODE_DIRECTORY) {
                dir_count_actual++;
            }
        }
    }
    
    char buf[80] = "Files: ";
    char temp[12];
    int_to_str(file_count_actual, temp);
    str_concat(buf, temp);
    str_concat(buf, ", Dirs: ");
    int_to_str(dir_count_actual, temp);
    str_concat(buf, temp);
    str_concat(buf, ", Total: ");
    int_to_str(file_count_actual + dir_count_actual, temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str(MAX_NODES, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_df_command(char* video, int* cursor) {
    int used = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (node_table[i].used) used++;
    }
    
    char buf[64] = "Used nodes: ";
    char temp[12];
    int_to_str(used, temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str(MAX_NODES, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_fscheck_command(char* video, int* cursor) {
    uint32_t active_generation = 0;
    int slot_validity[2] = {0, 0};
    fs_get_status(&active_generation, slot_validity);

    char buf[64];
    char temp[16];

    buf[0] = 0;
    str_concat(buf, "FS active gen: ");
    int_to_str((int)active_generation, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);

    buf[0] = 0;
    str_concat(buf, "Slot0: ");
    str_concat(buf, slot_validity[0] ? "valid" : "invalid");
    str_concat(buf, " | Slot1: ");
    str_concat(buf, slot_validity[1] ? "valid" : "invalid");
    print_string(buf, -1, video, cursor, 0xB);
}

static void handle_ver_command(char* video, int* cursor) {
    print_string("Smiggles OS v1.0.0\nDeveloped by Jules Miller and Vajra Vanukuri", -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_uptime_command(char* video, int* cursor) {
    char buf[64] = "Uptime: ";
    char temp[12];
    int_to_str(ticks / 18, temp);
    str_concat(buf, temp);
    str_concat(buf, " seconds");
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_halt_command(char* video, int* cursor) {
    handle_clear_command(video, cursor);
    print_string("Shutting down...", 15, video, cursor, COLOR_RED);
    // Shutdown for QEMU
    asm volatile("outw %0, %1" : : "a"((unsigned short)0x2000), "Nd"((unsigned short)0x604));
    while (1) {}
}

static void handle_reboot_command() {
    asm volatile ("int $0x19"); // BIOS reboot interrupt
}

static void byte_to_hex(unsigned char byte, char* buf) {
    const char hex_chars[] = "0123456789ABCDEF";
    buf[0] = hex_chars[(byte >> 4) & 0xF];
    buf[1] = hex_chars[byte & 0xF];
    buf[2] = ' ';
    buf[3] = 0;
}

static void handle_hexdump_command(const char* filename, char* video, int* cursor) {
    int node_idx = resolve_path(filename);
    if (node_idx == -1 || node_table[node_idx].type != NODE_FILE) {
        print_string("File not found", 14, video, cursor, COLOR_RED);
        return;
    }
    char buf[4];
    for (int i = 0; i < node_table[node_idx].content_size; i++) {
        byte_to_hex((unsigned char)node_table[node_idx].content[i], buf);
        print_string(buf, -1, video, cursor, COLOR_CYAN);
    }
}

static void handle_history_command(char* video, int* cursor) {
    for (int i = 0; i < history_count; i++) {
        print_string(history[i], -1, video, cursor, COLOR_LIGHT_GRAY);
    }
}

static void handle_pwd_command(char* video, int* cursor) {
    char path[MAX_PATH_LENGTH];
    get_full_path(current_dir_idx, path, MAX_PATH_LENGTH);
    print_string(path, -1, video, cursor, COLOR_CYAN);
}

static void handle_touch_command(const char* filename, char* video, int* cursor) {
    while (*filename == ' ') filename++;
    if (*filename == 0) {
        print_string("Usage: touch <filename>", 23, video, cursor, COLOR_RED);
        return;
    }
    char clean_name[MAX_PATH_LENGTH];
    int n = 0;
    while (filename[n] && n < MAX_PATH_LENGTH - 1) {
        clean_name[n] = filename[n];
        n++;
    }
    while (n > 0 && clean_name[n - 1] == ' ') n--;
    clean_name[n] = 0;
    if (clean_name[0] == 0) {
        print_string("Usage: touch <filename>", 23, video, cursor, COLOR_RED);
        return;
    }

    int existing_idx = resolve_path(clean_name);
    if (existing_idx != -1 && node_table[existing_idx].type == NODE_FILE) {
        print_file_already_exists_message(existing_idx, video, cursor);
        return;
    }

    int result = fs_touch(clean_name, "");
    if (result < 0) {
        print_string("Cannot create file", 18, video, cursor, COLOR_RED);
    } else {
        print_string("File created", 12, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_tree_command(char* video, int* cursor) {
    void print_tree(int node_idx, int depth, char* video, int* cursor) {
        if (node_idx < 0 || node_idx >= MAX_NODES || !node_table[node_idx].used) return;
        
        // Print indentation
        for (int i = 0; i < depth; i++) {
            print_string_sameline("  ", 2, video, cursor, 0xB);
        }
        
        if (node_table[node_idx].type == NODE_DIRECTORY) {
            print_string(node_table[node_idx].name, -1, video, cursor, 0xB);
            print_string_sameline("/", 1, video, cursor, 0xB);
            
            for (int i = 0; i < node_table[node_idx].child_count; i++) {
                print_tree(node_table[node_idx].children_idx[i], depth + 1, video, cursor);
            }
        } else {
            print_string(node_table[node_idx].name, -1, video, cursor, 0x0F);
        }
    }
    
    char path[MAX_PATH_LENGTH];
    get_full_path(current_dir_idx, path, MAX_PATH_LENGTH);
    print_string(path, -1, video, cursor, 0xE);
    print_tree(current_dir_idx, 0, video, cursor);
}

static void handle_grep_command(const char* args, char* video, int* cursor) {
    // Parse pattern and filename
    char pattern[64], filename[MAX_PATH_LENGTH];
    int i = 0, j = 0;
    
    // Skip leading spaces
    while (args[i] == ' ') i++;
    
    // Extract pattern (first argument), supports quoted patterns
    if (args[i] == '"') {
        i++; // skip opening quote
        while (args[i] && args[i] != '"' && j < 63) {
            pattern[j++] = args[i++];
        }
        if (args[i] == '"') i++; // skip closing quote
    } else {
        while (args[i] && args[i] != ' ' && j < 63) {
            pattern[j++] = args[i++];
        }
    }
    pattern[j] = 0;
    
    // Skip spaces
    while (args[i] == ' ') i++;
    
    // Extract filename (second argument), supports quoted filenames
    j = 0;
    if (args[i] == '"') {
        i++; // skip opening quote
        while (args[i] && args[i] != '"' && j < MAX_PATH_LENGTH - 1) {
            filename[j++] = args[i++];
        }
        if (args[i] == '"') i++; // skip closing quote
    } else {
        while (args[i] && args[i] != ' ' && j < MAX_PATH_LENGTH - 1) {
            filename[j++] = args[i++];
        }
    }
    filename[j] = 0;
    
    if (pattern[0] == 0 || filename[0] == 0) {
        print_string("Usage: grep pattern filename", 28, video, cursor, 0xC);
        return;
    }
    
    // Find the file
    int file_idx = resolve_path(filename);
    if (file_idx == -1 || node_table[file_idx].type != NODE_FILE) {
        print_string("File not found", 14, video, cursor, 0xC);
        return;
    }
    
    // Search through file content line by line
    char* content = node_table[file_idx].content;
    int content_size = node_table[file_idx].content_size;
    char line[MAX_FILE_CONTENT];
    int line_pos = 0;
    int match_found = 0;
    
    for (int i = 0; i <= content_size; i++) {
        if (i == content_size || content[i] == '\n') {
            line[line_pos] = 0;
            
            // Simple substring search
            int pattern_len = str_len(pattern);
            int line_len = line_pos;
            
            for (int start = 0; start <= line_len - pattern_len; start++) {
                int match = 1;
                for (int k = 0; k < pattern_len; k++) {
                    if (line[start + k] != pattern[k]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    match_found = 1;
                    print_string(line, line_pos, video, cursor, 0x0A);
                    break;
                }
            }
            
            line_pos = 0;
        } else {
            if (line_pos < MAX_FILE_CONTENT - 1) {
                line[line_pos++] = content[i];
            }
        }
    }
    
    if (!match_found) {
        print_string("No matches found", 16, video, cursor, 0x08);
    }
}

static void handle_cp_command(const char* args, char* video, int* cursor) {
    // Parse source and destination
    char source[MAX_PATH_LENGTH], dest[MAX_PATH_LENGTH];
    int i = 0, j = 0;
    
    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ') source[j++] = args[i++];
    source[j] = 0;
    
    while (args[i] == ' ') i++;
    j = 0;
    while (args[i]) dest[j++] = args[i++];
    dest[j] = 0;
    
    int src_idx = resolve_path(source);
    if (src_idx == -1 || node_table[src_idx].type != NODE_FILE) {
        print_string("Source file not found", 21, video, cursor, 0xC);
        return;
    }
    
    int result = fs_touch(dest, node_table[src_idx].content);
    if (result < 0) {
        print_string("Cannot create destination file", 30, video, cursor, 0xC);
    } else {
        node_table[result].content_size = node_table[src_idx].content_size;
        print_string("File copied", 11, video, cursor, 0xA);
    }
}

// --- Main Command Dispatcher ---
void dispatch_command(const char* cmd, char* video, int* cursor) {
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
    
    add_to_history(cmd);
    
    if (mini_strcmp(cmd, "pwd") == 0) {
        handle_pwd_command(video, cursor);
    } else if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
        handle_cd_command(cmd + 3, video, cursor, 0x0B);
    } else if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == 0) {
        handle_cd_command("", video, cursor, 0x0B);
    } else if (mini_strcmp(cmd, "ls") == 0) {
        handle_ls_command(video, cursor, 0x0B);
    } else if (cmd[0] == 'f' && cmd[1] == 'i' && cmd[2] == 'l' && cmd[3] == 'e' && cmd[4] == 's' && cmd[5] == 'i' && cmd[6] == 'z' && cmd[7] == 'e' && cmd[8] == ' ') {
        handle_filesize_command(cmd + 9, video, cursor);
    } else if (cmd[0] == 'm' && cmd[1] == 'k' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && cmd[5] == ' ') {
        handle_mkdir_command(cmd + 6, video, cursor, 0x0B);
    } else if (cmd[0] == 't' && cmd[1] == 'o' && cmd[2] == 'u' && cmd[3] == 'c' && cmd[4] == 'h' && cmd[5] == ' ') {
        handle_touch_command(cmd + 6, video, cursor);
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
        handle_cat_command(cmd + 4, video, cursor, 0x0E);
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && cmd[5] == ' ') {
        handle_rmdir_command(cmd + 6, video, cursor);
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ') {
        handle_rm_command(cmd + 3, video, cursor, 0x0C);
    } else if (mini_strcmp(cmd, "tree") == 0) {
        handle_tree_command(video, cursor);
    } else if (cmd[0] == 'c' && cmd[1] == 'p' && cmd[2] == ' ') {
        handle_cp_command(cmd + 3, video, cursor);
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
        char text[MAX_FILE_CONTENT];
        for (int i = 0; i < text_len && i < MAX_FILE_CONTENT-1; i++) text[i] = cmd[quote_start + 1 + i];
        text[text_len] = 0;
        char filename[MAX_FILE_NAME];
        int fn = 0;
        int fi = gt + 1;
        while (cmd[fi]) {
            if (cmd[fi] != ' ' && fn < MAX_FILE_NAME-1) filename[fn++] = cmd[fi];
            fi++;
        }
        filename[fn] = 0;
        handle_echo_command(text, filename, video, cursor, 0x0A);
    } else if (mini_strcmp(cmd, "ping") == 0) {
        handle_command(cmd, video, cursor, "ping", "pong", COLOR_LIGHT_GREEN);
    } else if (mini_strcmp(cmd, "about") == 0) {
        handle_command(cmd, video, cursor, "about", "Smiggles OS is a lightweight operating system designed by Jules Miller and Vajra Vanukuri.", COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help") == 0) {
        handle_command(cmd, video, cursor, "help",
            "touch file.txt - create file\n"
            "mkdir <path> - create directory\n"
            "rmdir <path> - remove directory\n"
            "cat file.txt - read file \n"
            "rm <path> - remove file\n"
            "cp <location> <destination> - copy file\n"
            "mv <old> <new> - rename/move file\n"
            "grep <pattern> <file> - search in file\n"
            "tree - directory tree\n"
            "edit <file> - text editor \n"
            "echo \"text\" > <file> - write to file\n"
            "print \"text\" - print text\n"
            "time - UTC time\n"
            "clear - clear screen\n"
            "df - filesystem usage\n"
            "fscheck - fs slot health\n"
            "ver - version info\n"
            "uptime - system uptime\n"
            "halt - shutdown\n"
            "reboot - restart\n"
            "neofetch - system info\n"
            "filesize <filename> - shows size of file\n",
            COLOR_LIGHT_GRAY);

    } else if (is_math_expr(cmd)) {
        handle_calc_command(cmd, video, cursor);
    } else if (mini_strcmp(cmd, "lsall") == 0) {
        handle_lsall_command(video, cursor);
    } else if (mini_strcmp(cmd, "time") == 0) {
        handle_time_command(video, cursor, 0x0A);
    } else if (mini_strcmp(cmd, "clear") == 0 || mini_strcmp(cmd, "cls") == 0) {
        handle_clear_command(video, cursor);
    } else if (mini_strcmp(cmd, "neofetch") == 0) {
        handle_neofetch_command(video, cursor);
    } else if (mini_strcmp(cmd, "free") == 0) {
        handle_free_command(video, cursor);
    } else if (mini_strcmp(cmd, "df") == 0) {
        handle_df_command(video, cursor);
    } else if (mini_strcmp(cmd, "fscheck") == 0) {
        handle_fscheck_command(video, cursor);
    } else if (mini_strcmp(cmd, "ver") == 0) {
        handle_ver_command(video, cursor);
    } else if (mini_strcmp(cmd, "uptime") == 0) {
        handle_uptime_command(video, cursor);
    } else if (mini_strcmp(cmd, "halt") == 0) {
        handle_halt_command(video, cursor);
    } else if (mini_strcmp(cmd, "reboot") == 0) {
        handle_reboot_command();
    } else if (mini_strcmp(cmd,"filesize")==0){
        handle_filesize_command(cmd + 9, video, cursor);
    } else if (cmd[0] == 'g' && cmd[1] == 'r' && cmd[2] == 'e' && cmd[3] == 'p' && cmd[4] == ' ') {
        handle_grep_command(cmd + 5, video, cursor);
    } else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'x' && cmd[3] == 'd' && cmd[4] == 'u' && cmd[5] == 'm' && cmd[6] == 'p') {
        handle_hexdump_command(cmd + 8, video, cursor);
    } else if (mini_strcmp(cmd, "history") == 0) {
        handle_history_command(video, cursor);
    } else if (cmd[0] == 'p' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 'n' && cmd[4] == 't' && cmd[5] == ' ' && cmd[6] == '"') {
        int start = 7;
        int end = start;
        while (cmd[end] && cmd[end] != '"') end++;
        if (cmd[end] == '"') {
            print_string(&cmd[start], end - start, video, cursor, 0x0D);
        }
    }
}

void handle_tab_completion(char* cmd_buf, int* cmd_len, int* cmd_cursor, char* video, int* cursor, int line_start) {
    // Only complete at end of command for now
    if (*cmd_cursor != *cmd_len) return;
    
    // Null terminate current buffer
    cmd_buf[*cmd_len] = 0;
    
    // List of common commands
    const char* commands[] = {
        "ls", "cd", "pwd", "cat", "mkdir", "rmdir", "rm", "touch", "cp", "mv",
        "echo", "edit", "tree", "grep", "clear", "cls", "help", "time", "ping",
        "about", "ver", "halt", "reboot", "history", "df", "fscheck", "free", "uptime","filesize","neofetch"
    };
    int cmd_count = 28;
    
    // Find what we're trying to complete
    int word_start = *cmd_len;
    while (word_start > 0 && cmd_buf[word_start - 1] != ' ') word_start--;
    
    char partial[MAX_CMD_BUFFER];
    int partial_len = *cmd_len - word_start;
    if (partial_len > MAX_CMD_BUFFER - 1) partial_len = MAX_CMD_BUFFER - 1;
    for (int i = 0; i < partial_len; i++) {
        partial[i] = cmd_buf[word_start + i];
    }
    partial[partial_len] = 0;
    
    // Collect matches into global array
    tab_match_count = 0;
    
    // If at start of line, match commands
    if (word_start == 0) {
        for (int i = 0; i < cmd_count && tab_match_count < 32; i++) {
            int match = 1;
            for (int j = 0; j < partial_len; j++) {
                if (commands[i][j] != partial[j]) {
                    match = 0;
                    break;
                }
            }
            if (match && commands[i][partial_len] != 0) {
                str_copy(tab_matches[tab_match_count], commands[i], MAX_NAME_LENGTH);
                tab_match_count++;
            }
        }
    }
    
    // Also match files/directories in current directory
    FSNode* dir = &node_table[current_dir_idx];
    for (int i = 0; i < dir->child_count && tab_match_count < 32; i++) {
        int child_idx = dir->children_idx[i];
        FSNode* child = &node_table[child_idx];
        
        int match = 1;
        for (int j = 0; j < partial_len; j++) {
            if (child->name[j] != partial[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            str_copy(tab_matches[tab_match_count], child->name, MAX_NAME_LENGTH);
            if (child->type == NODE_DIRECTORY) {
                int len = str_len(tab_matches[tab_match_count]);
                if (len < MAX_NAME_LENGTH - 1) {
                    tab_matches[tab_match_count][len] = '/';
                    tab_matches[tab_match_count][len + 1] = 0;
                }
            }
            tab_match_count++;
        }
    }
    
    if (tab_match_count == 0) {
        return;
    } else if (tab_match_count == 1) {
        // Single match - complete it
        const char* completion = tab_matches[0];
        int comp_len = str_len(completion);
        
        for (int i = word_start; i < *cmd_len; i++) {
            video[(line_start + i)*2] = ' ';
            video[(line_start + i)*2+1] = 0x07;
        }
        
        *cmd_len = word_start;
        for (int i = 0; i < comp_len && *cmd_len < (MAX_CMD_BUFFER - 1); i++) {
            cmd_buf[*cmd_len] = completion[i];
            video[(line_start + *cmd_len)*2] = completion[i];
            video[(line_start + *cmd_len)*2+1] = 0x0F;
            (*cmd_len)++;
        }
        
        *cmd_cursor = *cmd_len;
        *cursor = line_start + *cmd_len;
        
        unsigned short pos = *cursor;
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
        
        tab_completion_active = 0;
    } else {
        // Multiple matches - show list and enable browsing mode
        // Print all matches (each on new line)
        for (int i = 0; i < tab_match_count; i++) {
            print_string(tab_matches[i], -1, video, cursor, 0x0B);
        }
        
        // Reprint prompt and first match (no extra line needed)
        *cursor = ((*cursor / 80) + 1) * 80;
        while (*cursor >= 80*25) {
            scroll_screen(video);
            *cursor -= 80;
        }
        
        const char* prompt = "> ";
        int pi = 0;
        while (prompt[pi] && *cursor < 80*25 - 1) {
            video[(*cursor)*2] = prompt[pi];
            video[(*cursor)*2+1] = 0x0F;
            (*cursor)++;
            pi++;
        }
        
        int new_line_start = *cursor;
        
        // Load first completion
        *cmd_len = 0;
        int j = 0;
        while (tab_matches[0][j] && *cmd_len < (MAX_CMD_BUFFER - 1)) {
            cmd_buf[*cmd_len] = tab_matches[0][j];
            video[(new_line_start + *cmd_len)*2] = tab_matches[0][j];
            video[(new_line_start + *cmd_len)*2+1] = 0x0F;
            (*cmd_len)++;
            j++;
        }
        
        *cmd_cursor = *cmd_len;
        *cursor = new_line_start + *cmd_len;
        
        // Enable tab completion browsing mode
        tab_completion_active = 1;
        tab_completion_position = 0;
        
        unsigned short pos = *cursor;
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
        asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
    }
}