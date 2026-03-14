// ...existing code...
#include "kernel.h"
// Declare read_line function
void read_line(char* buf, int max_len, char* video, int* cursor);
// Declare get_key function
char get_key(void);

// Simple read_line implementation for login
void read_line(char* buf, int max_len, char* video, int* cursor) {
    int len = 0;
    while (len < max_len - 1) {
        char c = get_key(); // You may need to replace get_key with your actual key reading function
        if (c == '\n' || c == '\r') break;
        if (c == '\b' && len > 0) {
            len--;
            buf[len] = 0;
            print_string("\b \b", 3, video, cursor, COLOR_LIGHT_CYAN);
        } else if (c >= 32 && c <= 126) {
            buf[len++] = c;
            buf[len] = 0;
            print_string(&c, 1, video, cursor, COLOR_LIGHT_CYAN);
        }
    }
    buf[len] = 0;
}

static void handle_filesize_command(const char* filename, char* video, int* cursor) {
    int node_idx = resolve_path(filename);
    if (node_idx == -1) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    if (node_table[node_idx].type != NODE_FILE) {
        print_string("Not a file", 10, video, cursor, COLOR_LIGHT_RED);
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

// --- User Authentication ---
void handle_login_command(char* video, int* cursor) {
    extern User user_table[MAX_USERS];
    extern int user_count;
    extern int current_user_idx;

    extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
    char username[MAX_NAME_LENGTH];
    char password[MAX_NAME_LENGTH];
    shell_read_line("Username: ", username, MAX_NAME_LENGTH, video, cursor);
    shell_read_line("Password: ", password, MAX_NAME_LENGTH, video, cursor);

    for (int i = 0; i < user_count; i++) {
        if (mini_strcmp(username, user_table[i].username) == 0 && mini_strcmp(password, user_table[i].password) == 0) {
            current_user_idx = i;
            print_string("Login successful!", -1, video, cursor, COLOR_LIGHT_GREEN);
            return;
        }
    }
    print_string("Login failed.", -1, video, cursor, COLOR_LIGHT_RED);
}

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

    print_string(message, -1, video, cursor, COLOR_LIGHT_RED);
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
                print_string_sameline("\xDB\xDB\xDB", 3, video, cursor, COLOR_LIGHT_RED); // red
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
        print_string(buf, -1, video, cursor, COLOR_LIGHT_CYAN);
    }
}

static void handle_cat_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int node_idx = resolve_path(filename);
    if (node_idx == -1) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    if (node_table[node_idx].type != NODE_FILE) {
        print_string("Not a file", 10, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    if (node_table[node_idx].content_size <= 0) {
        print_string("File is empty", 13, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    extern int current_user_idx;
    extern User user_table[MAX_USERS];
    int allowed = 0;
    if (current_user_idx >= 0) {
        if (node_table[node_idx].owner_idx == current_user_idx || user_table[current_user_idx].is_admin) {
            allowed = 1;
        } else {
            // Check others read permission (last 3 bits)
            if ((node_table[node_idx].permissions & 0x4) != 0) {
                allowed = 1;
            }
        }
    }
    if (!allowed) {
        print_string("Permission denied.", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    print_string(node_table[node_idx].content, node_table[node_idx].content_size, video, cursor, COLOR_LIGHT_CYAN);
}

static void handle_echo_command(const char* text, const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int node_idx = resolve_path(filename);
    if (node_idx != -1) {
        if (node_table[node_idx].type == NODE_FILE) {
            print_file_already_exists_message(node_idx, video, cursor);
        } else {
            print_string("Cannot write file", 17, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Cannot write file", 17, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_rm_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    int idx = resolve_path(filename);
    if (idx == -1 || !node_table[idx].used) {
        print_string("File not found or cannot remove root", 37, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    extern int current_user_idx;
    extern User user_table[MAX_USERS];
    if (current_user_idx < 0 || (node_table[idx].owner_idx != current_user_idx && !user_table[current_user_idx].is_admin)) {
        print_string("Permission denied.", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    int result = fs_rm(filename, 0);
    if (result == -2) {
        print_string("Directory not empty. Use rmdir -r", 33, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Source not found", 16, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    extern int current_user_idx;
    extern User user_table[MAX_USERS];
    if (current_user_idx < 0 || (node_table[src_idx].owner_idx != current_user_idx && !user_table[current_user_idx].is_admin)) {
        print_string("Permission denied.", -1, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Parent directory not found", 26, video, cursor, COLOR_LIGHT_RED);
    } else if (result == -2) {
        print_string("Directory already exists", 24, video, cursor, COLOR_LIGHT_RED);
    } else if (result == -3) {
        print_string("No space for new directory", 26, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    if (node_table[target_idx].type != NODE_DIRECTORY) {
        print_string("Not a directory", 15, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
    } else if (result == -2) {
        print_string("Directory not empty. Use -r flag", 32, video, cursor, COLOR_LIGHT_RED);
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

static int parse_nonneg_int(const char* s, int* out) {
    int value = 0;
    int i = 0;
    if (!s || !out) return 0;
    while (s[i] == ' ') i++;
    if (s[i] == 0) return 0;
    while (s[i] && s[i] != ' ') {
        if (s[i] < '0' || s[i] > '9') return 0;
        value = value * 10 + (s[i] - '0');
        i++;
    }
    while (s[i] == ' ') i++;
    if (s[i] != 0) return 0;
    *out = value;
    return 1;
}

static int parse_ipv4_text(const char* s, uint8_t out_ip[4]) {
    int part = 0;
    int value = 0;
    int seen_digit = 0;

    if (!s || !out_ip) return 0;

    while (*s == ' ') s++;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            seen_digit = 1;
            value = value * 10 + (*s - '0');
            if (value > 255) return 0;
        } else if (*s == '.') {
            if (!seen_digit || part > 2) return 0;
            out_ip[part++] = (uint8_t)value;
            value = 0;
            seen_digit = 0;
        } else if (*s == ' ') {
        } else {
            return 0;
        }
        s++;
    }

    if (!seen_digit || part != 3) return 0;
    out_ip[3] = (uint8_t)value;
    return 1;
}

static void handle_spawn_command(const char* arg, char* video, int* cursor) {
    while (*arg == ' ') arg++;

    if (!(arg[0] == 'd' && arg[1] == 'e' && arg[2] == 'm' && arg[3] == 'o' && (arg[4] == 0 || arg[4] == ' '))) {
        print_string("Usage: spawn demo [count|auto on|auto off]", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    arg += 4;
    while (*arg == ' ') arg++;

    if (arg[0] == 0) {
        int pid = process_spawn_demo();
        if (pid < 0) {
            print_string("Failed to spawn process", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        schedule();
        char buf[64];
        char temp[16];
        buf[0] = 0;
        str_concat(buf, "Spawned demo process pid=");
        int_to_str(pid, temp);
        str_concat(buf, temp);
        print_string(buf, -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    if (arg[0] == 'a' && arg[1] == 'u' && arg[2] == 't' && arg[3] == 'o' && arg[4] == ' ') {
        const char* mode = arg + 5;
        while (*mode == ' ') mode++;
        if (mode[0] == 'o' && mode[1] == 'n' && mode[2] == 0) {
            process_set_demo_autorespawn(1);
            print_string("Demo auto-respawn: ON", -1, video, cursor, COLOR_LIGHT_GREEN);
            return;
        }
        if (mode[0] == 'o' && mode[1] == 'f' && mode[2] == 'f' && mode[3] == 0) {
            process_set_demo_autorespawn(0);
            print_string("Demo auto-respawn: OFF", -1, video, cursor, COLOR_LIGHT_GREEN);
            return;
        }
        print_string("Usage: spawn demo auto on|off", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int count = 0;
    if (!parse_nonneg_int(arg, &count) || count <= 0) {
        print_string("Usage: spawn demo [count|auto on|auto off]", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int spawned = 0;
    for (int i = 0; i < count; i++) {
        if (process_spawn_demo() >= 0) spawned++;
        else break;
    }

    if (spawned > 0) schedule();

    char buf[64];
    char temp[16];
    buf[0] = 0;
    str_concat(buf, "Spawned demo processes: ");
    int_to_str(spawned, temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str(count, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, spawned > 0 ? COLOR_LIGHT_GREEN : COLOR_LIGHT_RED);
}

static void handle_wait_command(const char* arg, char* video, int* cursor) {
    int wait_ticks = 0;
    if (!parse_nonneg_int(arg, &wait_ticks)) {
        print_string("Usage: wait <ticks>", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    unsigned int end_ticks = syscall_invoke1(3, (unsigned int)wait_ticks);

    char buf[64];
    char temp[16];
    buf[0] = 0;
    str_concat(buf, "Wait done at tick ");
    int_to_str((int)end_ticks, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_ps_command(char* video, int* cursor) {
    print_string("PID NAME   STATE    RUN   WORK", -1, video, cursor, COLOR_LIGHT_CYAN);

    for (int i = 0; i < MAX_PROCESSES; i++) {
        PCB* proc = &process_table[i];
        if (proc->state == PROC_UNUSED) continue;

        char line[96];
        char temp[16];

        line[0] = 0;
        int_to_str(proc->pid, temp);
        str_concat(line, temp);
        str_concat(line, "   ");

        if (proc->name[0]) str_concat(line, proc->name);
        else str_concat(line, "-");
        str_concat(line, "   ");

        str_concat(line, process_state_name(proc->state));
        str_concat(line, "   ");

        int_to_str((int)proc->run_ticks, temp);
        str_concat(line, temp);
        str_concat(line, "   ");

        int_to_str((int)proc->regs[0], temp);
        str_concat(line, temp);

        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
    }
}

static void handle_kill_command(const char* arg, char* video, int* cursor) {
    int pid = 0;
    if (!parse_nonneg_int(arg, &pid)) {
        print_string("Usage: kill <pid>", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int result = process_kill(pid);
    if (result == -1) {
        print_string("Invalid pid", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    if (result == -2) {
        print_string("Process slot is unused", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    print_string("Process killed", -1, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_syscalltest_command(char* video, int* cursor) {
    char buf[64];
    char temp[16];

    unsigned int before = syscall_invoke(1);
    unsigned int pid = syscall_invoke(2);
    unsigned int yield_ret = syscall_invoke(0);
    unsigned int after = syscall_invoke(1);
    unsigned int waited = syscall_invoke1(3, 2);

    buf[0] = 0;
    str_concat(buf, "sys_get_ticks before: ");
    int_to_str((int)before, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);

    buf[0] = 0;
    str_concat(buf, "sys_get_pid: ");
    if (pid == 0xFFFFFFFFu) {
        str_concat(buf, "none");
    } else {
        int_to_str((int)pid, temp);
        str_concat(buf, temp);
    }
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);

    buf[0] = 0;
    str_concat(buf, "sys_yield return: ");
    int_to_str((int)yield_ret, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);

    buf[0] = 0;
    str_concat(buf, "sys_get_ticks after: ");
    int_to_str((int)after, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);

    buf[0] = 0;
    str_concat(buf, "sys_wait_ticks(2) end: ");
    int_to_str((int)waited, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_fdtest_command(const char* arg, char* video, int* cursor) {
    char path[MAX_PATH_LENGTH];
    int path_len = 0;

    while (arg[path_len] == ' ') path_len++;
    if (arg[path_len] == 0) {
        print_string("Usage: fdtest <path>", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int src = path_len;
    int dst = 0;
    while (arg[src] && dst < MAX_PATH_LENGTH - 1) {
        path[dst++] = arg[src++];
    }
    while (dst > 0 && path[dst - 1] == ' ') dst--;
    path[dst] = 0;

    if (path[0] == 0) {
        print_string("Usage: fdtest <path>", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    const char* payload = "fdtest: syscall write/read OK";
    int payload_len = str_len(payload);
    char readback[64];
    for (int i = 0; i < (int)sizeof(readback); i++) readback[i] = 0;

    int fdw = (int)syscall_invoke2(SYS_OPEN, (unsigned int)path, (unsigned int)(FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC));
    if (fdw < 0) {
        print_string("fdtest: open(write) failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int wrote = (int)syscall_invoke3(SYS_WRITE, (unsigned int)fdw, (unsigned int)payload, (unsigned int)payload_len);
    syscall_invoke1(SYS_CLOSE, (unsigned int)fdw);
    if (wrote != payload_len) {
        print_string("fdtest: write failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int fdr = (int)syscall_invoke2(SYS_OPEN, (unsigned int)path, (unsigned int)FS_O_READ);
    if (fdr < 0) {
        print_string("fdtest: open(read) failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int readn = (int)syscall_invoke3(SYS_READ, (unsigned int)fdr, (unsigned int)readback, (unsigned int)(sizeof(readback) - 1));
    syscall_invoke1(SYS_CLOSE, (unsigned int)fdr);
    if (readn < 0) {
        print_string("fdtest: read failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    readback[readn] = 0;
    if (mini_strcmp(readback, payload) != 0) {
        print_string("fdtest: content mismatch", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    print_string("fdtest: PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_halt_command(char* video, int* cursor) {
    handle_clear_command(video, cursor);
    print_string("Shutting down...", 15, video, cursor, COLOR_LIGHT_RED);
    // Shutdown for QEMU
    asm volatile("outw %0, %1" : : "a"((unsigned short)0x2000), "Nd"((unsigned short)0x604));
    while (1) {}
}

static void handle_reboot_command() {
    asm volatile ("int $0x19"); // BIOS reboot interrupt
}

static void handle_panic_command(void) {
    kernel_panic("Manual panic requested", "Triggered from shell command.");
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
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Usage: touch <filename>", 23, video, cursor, COLOR_LIGHT_RED);
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
        print_string("Usage: touch <filename>", 23, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int existing_idx = resolve_path(clean_name);
    if (existing_idx != -1 && node_table[existing_idx].type == NODE_FILE) {
        print_file_already_exists_message(existing_idx, video, cursor);
        return;
    }

    int result = fs_touch(clean_name, "");
    if (result < 0) {
        print_string("Cannot create file", 18, video, cursor, COLOR_LIGHT_RED);
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
        print_string("No matches found", 16, video, cursor, COLOR_LIGHT_RED);
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

    /* commented out for now




    extern int current_user_idx;
    extern int request_login_screen;

    if (cmd[0] == 0) {
        return;
    }

    if (mini_strcmp(cmd, "logout") == 0) {
        current_user_idx = -1;
        request_login_screen = 1;
        return;
    }

    if (current_user_idx < 0) {
        print_string("Access denied: login required.", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    
    
    
    */



    // chmod: allow owner and admin, new format: chmod <filename>
    if (cmd[0] == 'c' && cmd[1] == 'h' && cmd[2] == 'm' && cmd[3] == 'o' && cmd[4] == 'd' && cmd[5] == ' ') {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        if (current_user_idx < 0) {
            print_string("Permission denied: only logged-in users can change permissions.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        int start = 6;
        while (cmd[start] == ' ') start++;
        char filename[MAX_NAME_LENGTH];
        int fn = 0;
        while (cmd[start] && fn < MAX_NAME_LENGTH-1) filename[fn++] = cmd[start++];
        filename[fn] = 0;
        int idx = resolve_path(filename);
        if (idx == -1 || !node_table[idx].used) {
            print_string("File not found.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (node_table[idx].owner_idx != current_user_idx && !user_table[current_user_idx].is_admin) {
            print_string("Permission denied: only owner or admin can change permissions.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        char perm_str[16];
        shell_read_line("Permissions (e.g. 600, 644, or 666): ", perm_str, 16, video, cursor);
        unsigned short perms = 0;
        int symbolic = 0;
        for (int i = 0; perm_str[i]; i++) {
            if (perm_str[i] == 'r' || perm_str[i] == 'w' || perm_str[i] == 'x' || perm_str[i] == '-') {
                symbolic = 1;
                break;
            }
        }
        if (symbolic) {
            if (str_len(perm_str) < 9) {
                print_string("Invalid symbolic permissions.", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
            for (int i = 0; i < 9; i++) {
                int bit = 0;
                if (perm_str[i] == 'r') bit = 4;
                else if (perm_str[i] == 'w') bit = 2;
                else if (perm_str[i] == 'x') bit = 1;
                else if (perm_str[i] == '-') bit = 0;
                else {
                    print_string("Invalid character in permissions.", -1, video, cursor, COLOR_LIGHT_RED);
                    return;
                }
                perms |= bit << (8 - i);
            }
        } else {
            for (int i = 0; perm_str[i] && i < 4; i++) {
                if (perm_str[i] < '0' || perm_str[i] > '7') {
                    print_string("Invalid octal permissions.", -1, video, cursor, COLOR_LIGHT_RED);
                    return;
                }
                perms = perms * 8 + (perm_str[i] - '0');
            }
        }
        node_table[idx].permissions = perms;
        fs_save();
        print_string("Permissions updated.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    // chown: admin only, new format: chown <filename>
    if (cmd[0] == 'c' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'w' && cmd[4] == 'n' && cmd[5] == ' ') {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        if (current_user_idx < 0 || !user_table[current_user_idx].is_admin) {
            print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        int start = 6;
        while (cmd[start] == ' ') start++;
        char filename[MAX_NAME_LENGTH];
        int fn = 0;
        while (cmd[start] && fn < MAX_NAME_LENGTH-1) filename[fn++] = cmd[start++];
        filename[fn] = 0;
        int idx = resolve_path(filename);
        if (idx == -1 || !node_table[idx].used) {
            print_string("File not found.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        char username[MAX_NAME_LENGTH];
        shell_read_line("New owner username: ", username, MAX_NAME_LENGTH, video, cursor);
        int owner_idx = -1;
        for (int i = 0; i < user_count; i++) {
            if (mini_strcmp(username, user_table[i].username) == 0) {
                owner_idx = i;
                break;
            }
        }
        if (owner_idx == -1) {
            print_string("User not found.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        node_table[idx].owner_idx = owner_idx;
        fs_save();
        print_string("Owner updated.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    // edituser command
    if (mini_strcmp(cmd, "edituser") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        extern void fs_save();
        int target_idx = -1;
        if (current_user_idx < 0) {
            print_string("Not logged in.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (user_table[current_user_idx].is_admin) {
            char username[MAX_NAME_LENGTH];
            shell_read_line("Current username: ", username, MAX_NAME_LENGTH, video, cursor);
            for (int i = 0; i < user_count; i++) {
                if (mini_strcmp(username, user_table[i].username) == 0) {
                    target_idx = i;
                    break;
                }
            }
            if (target_idx == -1) {
                print_string("User not found.", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
        } else {
            target_idx = current_user_idx;
        }
        char new_username[MAX_NAME_LENGTH];
        char new_password[MAX_NAME_LENGTH];
        shell_read_line("New username: ", new_username, MAX_NAME_LENGTH, video, cursor);
        shell_read_line("New password: ", new_password, MAX_NAME_LENGTH, video, cursor);
        str_copy(user_table[target_idx].username, new_username, MAX_NAME_LENGTH);
        str_copy(user_table[target_idx].password, new_password, MAX_NAME_LENGTH);
        fs_save();
        print_string("User updated.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    // Debug command: dumpusers (prints all usernames and admin status)
    if (mini_strcmp(cmd, "dumpusers") == 0) {
        extern User user_table[MAX_USERS];
        extern int user_count;
        char buf[64];
        for (int i = 0; i < user_count; i++) {
            int n = 0;
            str_copy(buf+n, user_table[i].username, 32);
            n += str_len(user_table[i].username);
            str_copy(buf+n, " [", 3);
            n += str_len(" [");
            buf[n++] = user_table[i].is_admin ? 'A' : 'U';
            buf[n++] = ']';
            buf[n++] = 0;
            print_string(buf, -1, video, cursor, COLOR_LIGHT_CYAN);
        }
        return;
    }
    // Admin-only: adduser
    if (mini_strcmp(cmd, "adduser") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        if (current_user_idx < 0 || !user_table[current_user_idx].is_admin) {
            print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (user_count >= MAX_USERS) {
            print_string("User limit reached.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        char username[MAX_NAME_LENGTH];
        char password[MAX_NAME_LENGTH];
        shell_read_line("New username: ", username, MAX_NAME_LENGTH, video, cursor);
        // Check for duplicate username
        for (int i = 0; i < user_count; i++) {
            if (mini_strcmp(username, user_table[i].username) == 0) {
                print_string("Username exists.", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
        }
        shell_read_line("New password: ", password, MAX_NAME_LENGTH, video, cursor);
        user_table[user_count].is_admin = 0;
        str_copy(user_table[user_count].username, username, MAX_NAME_LENGTH);
        str_copy(user_table[user_count].password, password, MAX_NAME_LENGTH);
        user_count++;
        extern void fs_save();
        fs_save();
        print_string("User added.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    // Admin-only: deluser
    if (mini_strcmp(cmd, "deluser") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        if (current_user_idx < 0 || !user_table[current_user_idx].is_admin) {
            print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        char username[MAX_NAME_LENGTH];
        shell_read_line("Delete username: ", username, MAX_NAME_LENGTH, video, cursor);
        int idx = -1;
        int admin_count = 0;
        for (int i = 0; i < user_count; i++) {
            if (user_table[i].is_admin) admin_count++;
            if (mini_strcmp(username, user_table[i].username) == 0) idx = i;
        }
        if (idx == -1) {
            print_string("User not found.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (idx == current_user_idx) {
            print_string("Cannot delete current user.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (user_table[idx].is_admin && admin_count <= 1) {
            print_string("Cannot delete last admin.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        // Shift users
        for (int i = idx; i < user_count-1; i++) user_table[i] = user_table[i+1];
        user_count--;
        extern void fs_save();
        fs_save();
        print_string("User deleted.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    // Admin-only command: listusers
    if (mini_strcmp(cmd, "listusers") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        if (current_user_idx < 0 || !user_table[current_user_idx].is_admin) {
            print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        for (int i = 0; i < user_count; i++) {
            print_string(user_table[i].username, -1, video, cursor, COLOR_LIGHT_CYAN);
        }
        return;
    }
    extern int current_user_idx;
    //Restrict sensitive commands to logged-in users 
    //TODO: let regular 
    if ((cmd[0] == 'r' && cmd[1] == 'm' && (cmd[2] == ' ' || (cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r'))) || mini_strcmp(cmd, "useradd") == 0 || mini_strcmp(cmd, "userdel") == 0) {
        if (current_user_idx < 0) {
            print_string("Access denied: login required.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
    }
    if (mini_strcmp(cmd, "logout") == 0) {
        extern int current_user_idx;
        current_user_idx = -1;
        print_string("Logged out.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    if (mini_strcmp(cmd, "whoami") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        if (current_user_idx >= 0) {
            print_string(user_table[current_user_idx].username, -1, video, cursor, COLOR_LIGHT_CYAN);
        } else {
            print_string("guest", -1, video, cursor, COLOR_LIGHT_CYAN);
        }
        return;
    }
    if (mini_strcmp(cmd, "login") == 0) {
        handle_login_command(video, cursor);
        return;
    }
    // nano-like editor: edit filename.txt
    if (cmd[0] == 'e' && cmd[1] == 'd' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == ' ') {
        int start = 5;
        while (cmd[start] == ' ') start++;
        char filename[MAX_FILE_NAME];
        int fn = 0;
        while (cmd[start] && fn < MAX_FILE_NAME-1) filename[fn++] = cmd[start++];
        filename[fn] = 0;
        int idx = resolve_path(filename);
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        if (idx == -1 || !node_table[idx].used) {
            print_string("File not found", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (current_user_idx < 0 || (node_table[idx].owner_idx != current_user_idx && !user_table[current_user_idx].is_admin)) {
            print_string("Permission denied.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
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
    } else if (mini_strcmp(cmd, "pciscan") == 0) {
        pci_scan_and_print(video, cursor);
    } else if (mini_strcmp(cmd, "rtltest") == 0) {
        Rtl8139Status status;
        int result = 1;

        if (!rtl8139_get_status(&status) || !status.initialized) {
            result = rtl8139_init();
        }

        if (result == 1) {
            rtl8139_print_status(video, cursor);
        } else if (result == 0) {
            print_string("RTL8139: device not present", -1, video, cursor, COLOR_LIGHT_RED);
        } else if (result == -2) {
            print_string("RTL8139: invalid I/O base", -1, video, cursor, COLOR_LIGHT_RED);
        } else if (result == -3) {
            print_string("RTL8139: PCI enable failed", -1, video, cursor, COLOR_LIGHT_RED);
        } else if (result == -4) {
            print_string("RTL8139: reset timed out", -1, video, cursor, COLOR_LIGHT_RED);
        } else {
            print_string("RTL8139: init failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "rtltx") == 0) {
        Rtl8139Status status;
        unsigned char frame[64];
        int result;

        if (!rtl8139_get_status(&status) || !status.initialized) {
            result = rtl8139_init();
            if (result <= 0) {
                print_string("RTL8139: init required before tx", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
            rtl8139_get_status(&status);
        }

        for (int i = 0; i < 6; i++) frame[i] = 0xFF;
        for (int i = 0; i < 6; i++) frame[6 + i] = status.mac[i];
        frame[12] = 0x88;
        frame[13] = 0xB5;
        for (int i = 14; i < 60; i++) frame[i] = 0;
        frame[14] = 'S';
        frame[15] = 'M';
        frame[16] = 'I';
        frame[17] = 'G';
        frame[18] = 'G';
        frame[19] = 'L';
        frame[20] = 'E';
        frame[21] = 'S';

        result = rtl8139_send_frame(frame, 60);
        if (result > 0) {
            print_string("RTL8139: test frame queued", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("RTL8139: tx failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "rtlrx") == 0) {
        unsigned char frame[256];
        int length = 0;
        int result;
        char line[96];
        char value[24];
        const char* hex = "0123456789ABCDEF";

        result = rtl8139_poll_receive(frame, sizeof(frame), &length);
        if (result == 0) {
            print_string("RTL8139: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (result < 0) {
            print_string("RTL8139: rx failed", -1, video, cursor, COLOR_LIGHT_RED);
        } else {
            line[0] = 0;
            str_concat(line, "RTL8139: received ");
            int_to_str(length, value);
            str_concat(line, value);
            str_concat(line, " bytes type=0x");
            value[0] = hex[(frame[12] >> 4) & 0x0F];
            value[1] = hex[frame[12] & 0x0F];
            value[2] = hex[(frame[13] >> 4) & 0x0F];
            value[3] = hex[frame[13] & 0x0F];
            value[4] = 0;
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
        }
    } else if (mini_strcmp(cmd, "arp table") == 0) {
        int count = arp_get_cache_count();
        char line[96];
        char value[24];
        const char* hex = "0123456789ABCDEF";

        if (count == 0) {
            print_string("ARP: cache empty", -1, video, cursor, COLOR_YELLOW);
        } else {
            for (int i = 0; i < count; i++) {
                uint8_t ip[4];
                uint8_t mac[6];
                int p = 0;
                if (!arp_get_cache_entry(i, ip, mac)) continue;

                line[0] = 0;
                str_concat(line, "ARP ");
                int_to_str(ip[0], value); str_concat(line, value); str_concat(line, ".");
                int_to_str(ip[1], value); str_concat(line, value); str_concat(line, ".");
                int_to_str(ip[2], value); str_concat(line, value); str_concat(line, ".");
                int_to_str(ip[3], value); str_concat(line, value);
                str_concat(line, " -> ");

                value[p++] = hex[(mac[0] >> 4) & 0x0F]; value[p++] = hex[mac[0] & 0x0F]; value[p++] = ':';
                value[p++] = hex[(mac[1] >> 4) & 0x0F]; value[p++] = hex[mac[1] & 0x0F]; value[p++] = ':';
                value[p++] = hex[(mac[2] >> 4) & 0x0F]; value[p++] = hex[mac[2] & 0x0F]; value[p++] = ':';
                value[p++] = hex[(mac[3] >> 4) & 0x0F]; value[p++] = hex[mac[3] & 0x0F]; value[p++] = ':';
                value[p++] = hex[(mac[4] >> 4) & 0x0F]; value[p++] = hex[mac[4] & 0x0F]; value[p++] = ':';
                value[p++] = hex[(mac[5] >> 4) & 0x0F]; value[p++] = hex[mac[5] & 0x0F]; value[p++] = 0;

                str_concat(line, value);
                print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);
            }
        }
    } else if (cmd[0] == 'a' && cmd[1] == 'r' && cmd[2] == 'p' && cmd[3] == ' ' && cmd[4] == 's' && cmd[5] == 'e' && cmd[6] == 't' && cmd[7] == 'i' && cmd[8] == 'p' && cmd[9] == ' ') {
        uint8_t ip[4] = {0, 0, 0, 0};
        int part = 0;
        int value = 0;
        int seen_digit = 0;
        int ok = 1;
        const char* s = cmd + 10;

        while (*s == ' ') s++;
        while (*s && ok) {
            if (*s >= '0' && *s <= '9') {
                seen_digit = 1;
                value = value * 10 + (*s - '0');
                if (value > 255) ok = 0;
            } else if (*s == '.') {
                if (!seen_digit || part > 2) ok = 0;
                else {
                    ip[part++] = (uint8_t)value;
                    value = 0;
                    seen_digit = 0;
                }
            } else if (*s == ' ') {
            } else {
                ok = 0;
            }
            s++;
        }

        if (ok && seen_digit && part == 3) {
            ip[3] = (uint8_t)value;
            if (arp_set_local_ip(ip)) {
                print_string("ARP: local IP updated", -1, video, cursor, COLOR_LIGHT_GREEN);
            } else {
                print_string("ARP: failed to set local IP", -1, video, cursor, COLOR_LIGHT_RED);
            }
        } else {
            print_string("Usage: arp setip <a.b.c.d>", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (cmd[0] == 'a' && cmd[1] == 'r' && cmd[2] == 'p' && cmd[3] == ' ' && cmd[4] == 'w' && cmd[5] == 'h' && cmd[6] == 'o' && cmd[7] == 'h' && cmd[8] == 'a' && cmd[9] == 's' && cmd[10] == ' ') {
        uint8_t ip[4] = {0, 0, 0, 0};
        int part = 0;
        int value = 0;
        int seen_digit = 0;
        int ok = 1;
        const char* s = cmd + 11;

        while (*s == ' ') s++;
        while (*s && ok) {
            if (*s >= '0' && *s <= '9') {
                seen_digit = 1;
                value = value * 10 + (*s - '0');
                if (value > 255) ok = 0;
            } else if (*s == '.') {
                if (!seen_digit || part > 2) ok = 0;
                else {
                    ip[part++] = (uint8_t)value;
                    value = 0;
                    seen_digit = 0;
                }
            } else if (*s == ' ') {
            } else {
                ok = 0;
            }
            s++;
        }

        if (ok && seen_digit && part == 3) {
            ip[3] = (uint8_t)value;
            if (arp_send_request(ip) > 0) {
                print_string("ARP: request sent", -1, video, cursor, COLOR_LIGHT_GREEN);
            } else {
                print_string("ARP: request failed", -1, video, cursor, COLOR_LIGHT_RED);
            }
        } else {
            print_string("Usage: arp whohas <a.b.c.d>", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "arp poll") == 0) {
        int r = arp_poll_once();
        if (r == 0) {
            print_string("ARP: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 1) {
            print_string("ARP: packet processed", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (r == 2) {
            print_string("ARP: non-ARP frame ignored", -1, video, cursor, COLOR_YELLOW);
        } else {
            print_string("ARP: poll failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "ip poll") == 0) {
        int r = ipv4_poll_once();
        if (r == 0) {
            print_string("IP: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 1) {
            IPv4Stats stats;
            char line[96];
            char value[24];
            if (!ipv4_get_stats(&stats)) {
                print_string("IP: stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }

            line[0] = 0;
            str_concat(line, "IP: src ");
            int_to_str(stats.last_src_ip[0], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(stats.last_src_ip[1], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(stats.last_src_ip[2], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(stats.last_src_ip[3], value); str_concat(line, value);
            str_concat(line, " -> ");
            int_to_str(stats.last_dst_ip[0], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(stats.last_dst_ip[1], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(stats.last_dst_ip[2], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(stats.last_dst_ip[3], value); str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);

            line[0] = 0;
            str_concat(line, "IP: proto=");
            int_to_str(stats.last_protocol, value); str_concat(line, value);
            str_concat(line, " ttl=");
            int_to_str(stats.last_ttl, value); str_concat(line, value);
            str_concat(line, " len=");
            int_to_str(stats.last_total_length, value); str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);
        } else if (r == 2) {
            print_string("IP: non-IPv4 frame ignored", -1, video, cursor, COLOR_YELLOW);
        } else {
            print_string("IP: parse failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "ip stats") == 0) {
        IPv4Stats stats;
        char line[96];
        char value[24];

        if (!ipv4_get_stats(&stats)) {
            print_string("IP: stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        line[0] = 0;
        str_concat(line, "IP frames=");
        int_to_str((int)stats.frames_polled, value); str_concat(line, value);
        str_concat(line, " parsed=");
        int_to_str((int)stats.ipv4_parsed, value); str_concat(line, value);
        str_concat(line, " non-ip=");
        int_to_str((int)stats.non_ipv4_frames, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "IP err ver=");
        int_to_str((int)stats.bad_version, value); str_concat(line, value);
        str_concat(line, " ihl=");
        int_to_str((int)stats.bad_ihl, value); str_concat(line, value);
        str_concat(line, " len=");
        int_to_str((int)stats.bad_total_length, value); str_concat(line, value);
        str_concat(line, " csum=");
        int_to_str((int)stats.bad_checksum, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "icmp poll") == 0) {
        int r = icmp_poll_once();
        if (r == 0) {
            print_string("ICMP: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 1) {
            print_string("ICMP: packet processed", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (r == 2 || r == 3) {
            print_string("ICMP: non-ICMP frame ignored", -1, video, cursor, COLOR_YELLOW);
        } else {
            print_string("ICMP: poll failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "icmp stats") == 0) {
        ICMPStats stats;
        char line[96];
        char value[24];

        if (!icmp_get_stats(&stats)) {
            print_string("ICMP: stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        line[0] = 0;
        str_concat(line, "ICMP frames=");
        int_to_str((int)stats.frames_polled, value); str_concat(line, value);
        str_concat(line, " seen=");
        int_to_str((int)stats.icmp_seen, value); str_concat(line, value);
        str_concat(line, " errs=");
        int_to_str((int)stats.parse_errors, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "ICMP req=");
        int_to_str((int)stats.echo_requests, value); str_concat(line, value);
        str_concat(line, " rep-sent=");
        int_to_str((int)stats.echo_replies_sent, value); str_concat(line, value);
        str_concat(line, " rep-recv=");
        int_to_str((int)stats.echo_replies_received, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (cmd[0] == 'p' && cmd[1] == 'i' && cmd[2] == 'n' && cmd[3] == 'g' && cmd[4] == ' ') {
        uint8_t ip[4];
        int result;

        if (!parse_ipv4_text(cmd + 5, ip)) {
            print_string("Usage: ping <a.b.c.d>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        result = icmp_send_echo_request(ip, 0x534Du, (uint16_t)(ticks & 0xFFFF));
        if (result > 0) {
            print_string("ICMP: echo request sent", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (result == -4) {
            print_string("ICMP: target MAC unknown (run arp whohas first)", -1, video, cursor, COLOR_YELLOW);
        } else {
            print_string("ICMP: echo request failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "ping") == 0) {
        print_string("Usage: ping <a.b.c.d>", -1, video, cursor, COLOR_YELLOW);
    } else if (cmd[0] == 'u' && cmd[1] == 'd' && cmd[2] == 'p' && cmd[3] == ' ' && cmd[4] == 's' && cmd[5] == 'e' && cmd[6] == 'n' && cmd[7] == 'd' && cmd[8] == ' ') {
        uint8_t target_ip[4];
        char ip_buf[20];
        char port_buf[12];
        const char* p = cmd + 9;
        int i = 0;
        int j = 0;
        int dst_port = 0;
        int sent;

        while (*p == ' ') p++;
        while (*p && *p != ' ' && i < (int)sizeof(ip_buf) - 1) ip_buf[i++] = *p++;
        ip_buf[i] = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && j < (int)sizeof(port_buf) - 1) port_buf[j++] = *p++;
        port_buf[j] = 0;
        while (*p == ' ') p++;

        if (!parse_ipv4_text(ip_buf, target_ip) || !parse_nonneg_int(port_buf, &dst_port) || dst_port < 1 || dst_port > 65535 || *p == 0) {
            print_string("Usage: udp send <a.b.c.d> <port> <text>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        {
            int payload_len = str_len(p);
            if (payload_len > 512) payload_len = 512;
            sent = udp_send_datagram(target_ip, 40000, (uint16_t)dst_port, (const uint8_t*)p, payload_len);
        }

        if (sent > 0) {
            print_string("UDP: datagram sent", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (sent == -4) {
            print_string("UDP: target MAC unknown (run arp whohas first)", -1, video, cursor, COLOR_YELLOW);
        } else {
            print_string("UDP: send failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "udp poll") == 0) {
        int r = udp_poll_once();
        if (r == 0) {
            print_string("UDP: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 1) {
            print_string("UDP: packet processed", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (r == 2 || r == 3) {
            print_string("UDP: no supported protocol in frame", -1, video, cursor, COLOR_YELLOW);
        } else {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "UDP: parse failed code=");
            int_to_str(r, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "udp listen") == 0 || mini_strcmp(cmd, "udp listen show") == 0) {
        uint16_t port = 0;
        if (udp_get_listen_port(&port)) {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "UDP listen: ON port=");
            int_to_str((int)port, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("UDP listen: OFF", -1, video, cursor, COLOR_YELLOW);
        }
    } else if (mini_strcmp(cmd, "udp listen off") == 0) {
        udp_clear_listen_port();
        print_string("UDP listen: OFF", -1, video, cursor, COLOR_YELLOW);
    } else if (cmd[0] == 'u' && cmd[1] == 'd' && cmd[2] == 'p' && cmd[3] == ' ' && cmd[4] == 'l' && cmd[5] == 'i' && cmd[6] == 's' && cmd[7] == 't' && cmd[8] == 'e' && cmd[9] == 'n' && cmd[10] == ' ') {
        int port = 0;
        if (!parse_nonneg_int(cmd + 11, &port) || port < 1 || port > 65535) {
            print_string("Usage: udp listen <port>|off|show", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (udp_set_listen_port((uint16_t)port)) {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "UDP listen: ON port=");
            int_to_str(port, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("UDP listen: failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "udp recv") == 0) {
        uint8_t src_ip[4];
        uint16_t src_port;
        uint16_t dst_port;
        uint8_t payload[513];
        int payload_len = 0;
        int r = udp_recv_next(src_ip, &src_port, &dst_port, payload, 512, &payload_len);
        char line[128];
        char value[24];

        if (r == 0) {
            print_string("UDP: receive queue empty", -1, video, cursor, COLOR_YELLOW);
            return;
        }

        if (r < 0) {
            print_string("UDP: receive failed", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (payload_len < 0) payload_len = 0;
        if (payload_len > 512) payload_len = 512;
        payload[payload_len] = 0;

        line[0] = 0;
        str_concat(line, "UDP: ");
        int_to_str(src_ip[0], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[1], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[2], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[3], value); str_concat(line, value);
        str_concat(line, ":");
        int_to_str((int)src_port, value); str_concat(line, value);
        str_concat(line, " -> :");
        int_to_str((int)dst_port, value); str_concat(line, value);
        str_concat(line, " len=");
        int_to_str(payload_len, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);

        for (int k = 0; k < payload_len; k++) {
            if (payload[k] < 32 || payload[k] > 126) payload[k] = '.';
        }
        print_string((const char*)payload, payload_len, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "udp stats") == 0) {
        UDPStats stats;
        char line[96];
        char value[24];
        uint16_t listen_port = 0;

        if (!udp_get_stats(&stats)) {
            print_string("UDP: stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        line[0] = 0;
        str_concat(line, "UDP frames=");
        int_to_str((int)stats.frames_polled, value); str_concat(line, value);
        str_concat(line, " seen=");
        int_to_str((int)stats.udp_seen, value); str_concat(line, value);
        str_concat(line, " non-udp=");
        int_to_str((int)stats.non_udp_ipv4, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "UDP sent=");
        int_to_str((int)stats.sent_packets, value); str_concat(line, value);
        str_concat(line, " queued=");
        int_to_str((int)stats.recv_queued, value); str_concat(line, value);
        str_concat(line, " drop=");
        int_to_str((int)stats.recv_dropped, value); str_concat(line, value);
        str_concat(line, " err=");
        int_to_str((int)stats.parse_errors, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "UDP listen=");
        if (udp_get_listen_port(&listen_port)) {
            str_concat(line, "on:");
            int_to_str((int)listen_port, value); str_concat(line, value);
        } else {
            str_concat(line, "off");
        }
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "udp") == 0) {
        print_string("UDP usage: udp send|poll|recv|stats|listen", -1, video, cursor, COLOR_YELLOW);
    } else if (mini_strcmp(cmd, "net poll") == 0) {
        int r = net_poll_once();
        if (r == 0) {
            print_string("NET: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 1) {
            print_string("NET: packet processed", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (r == 2 || r == 3) {
            print_string("NET: no supported protocol in frame", -1, video, cursor, COLOR_YELLOW);
        } else {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "NET: parse failed code=");
            int_to_str(r, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "net") == 0) {
        print_string("NET usage: net poll", -1, video, cursor, COLOR_YELLOW);
    } else if (mini_strcmp(cmd, "tcp poll") == 0) {
        int r = tcp_poll_once();
        if (r == 0) {
            print_string("TCP: no packet available", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 1) {
            print_string("TCP: packet processed", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else if (r == 2 || r == 3) {
            print_string("TCP: no TCP packet in frame", -1, video, cursor, COLOR_YELLOW);
        } else {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "TCP: parse failed code=");
            int_to_str(r, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "tcp listen") == 0 || mini_strcmp(cmd, "tcp listen show") == 0) {
        uint16_t port = 0;
        if (tcp_get_listen_port(&port)) {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "TCP listen: ON port=");
            int_to_str((int)port, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("TCP listen: OFF", -1, video, cursor, COLOR_YELLOW);
        }
    } else if (mini_strcmp(cmd, "tcp listen off") == 0) {
        tcp_clear_listen_port();
        print_string("TCP listen: OFF", -1, video, cursor, COLOR_YELLOW);
    } else if (cmd[0] == 't' && cmd[1] == 'c' && cmd[2] == 'p' && cmd[3] == ' ' && cmd[4] == 'l' && cmd[5] == 'i' && cmd[6] == 's' && cmd[7] == 't' && cmd[8] == 'e' && cmd[9] == 'n' && cmd[10] == ' ') {
        int port = 0;
        if (!parse_nonneg_int(cmd + 11, &port) || port < 1 || port > 65535) {
            print_string("Usage: tcp listen <port>|off|show", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (tcp_set_listen_port((uint16_t)port)) {
            char line[64];
            char value[24];
            line[0] = 0;
            str_concat(line, "TCP listen: ON port=");
            int_to_str(port, value);
            str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("TCP listen: failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (mini_strcmp(cmd, "tcp stats") == 0) {
        TCPStats stats;
        char line[96];
        char value[24];
        uint16_t listen_port = 0;

        if (!tcp_get_stats(&stats)) {
            print_string("TCP: stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        line[0] = 0;
        str_concat(line, "TCP frames=");
        int_to_str((int)stats.frames_polled, value); str_concat(line, value);
        str_concat(line, " seen=");
        int_to_str((int)stats.tcp_seen, value); str_concat(line, value);
        str_concat(line, " err=");
        int_to_str((int)stats.parse_errors, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "TCP syn=");
        int_to_str((int)stats.syn_received, value); str_concat(line, value);
        str_concat(line, " synack=");
        int_to_str((int)stats.synack_sent, value); str_concat(line, value);
        str_concat(line, " est=");
        int_to_str((int)stats.established, value); str_concat(line, value);
        str_concat(line, " ack=");
        int_to_str((int)stats.ack_sent, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "TCP listen=");
        if (tcp_get_listen_port(&listen_port)) {
            str_concat(line, "on:");
            int_to_str((int)listen_port, value); str_concat(line, value);
        } else {
            str_concat(line, "off");
        }
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "tcp conns") == 0) {
        int count = tcp_get_conn_count();
        char line[128];
        char value[24];

        if (count <= 0) {
            print_string("TCP: no active connections", -1, video, cursor, COLOR_YELLOW);
            return;
        }

        for (int i = 0; i < count; i++) {
            TCPConnInfo info;
            const char* state = "UNK";
            if (!tcp_get_conn_info(i, &info)) continue;
            if (info.state == 1) state = "SYN_RCVD";
            else if (info.state == 2) state = "ESTABLISHED";

            line[0] = 0;
            int_to_str(info.src_ip[0], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(info.src_ip[1], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(info.src_ip[2], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(info.src_ip[3], value); str_concat(line, value);
            str_concat(line, ":");
            int_to_str((int)info.src_port, value); str_concat(line, value);
            str_concat(line, " -> ");
            int_to_str(info.dst_ip[0], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(info.dst_ip[1], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(info.dst_ip[2], value); str_concat(line, value); str_concat(line, ".");
            int_to_str(info.dst_ip[3], value); str_concat(line, value);
            str_concat(line, ":");
            int_to_str((int)info.dst_port, value); str_concat(line, value);
            str_concat(line, " ");
            str_concat(line, state);
            print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);
        }
    } else if (mini_strcmp(cmd, "tcp") == 0) {
        print_string("TCP usage: tcp listen|poll|stats|conns", -1, video, cursor, COLOR_YELLOW);
    } else if (mini_strcmp(cmd, "sock open udp") == 0) {
        int fd = sock_open_udp();
        char line[64];
        char value[24];
        if (fd < 0) {
            print_string("SOCK: open failed", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        line[0] = 0;
        str_concat(line, "SOCK: opened fd=");
        int_to_str(fd, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else if (cmd[0] == 's' && cmd[1] == 'o' && cmd[2] == 'c' && cmd[3] == 'k' && cmd[4] == ' ' && cmd[5] == 'b' && cmd[6] == 'i' && cmd[7] == 'n' && cmd[8] == 'd' && cmd[9] == ' ') {
        int fd = 0;
        int port = 0;
        char fd_buf[16];
        char port_buf[16];
        const char* p = cmd + 10;
        int i = 0;
        int j = 0;
        int r;

        while (*p == ' ') p++;
        while (*p && *p != ' ' && i < (int)sizeof(fd_buf) - 1) fd_buf[i++] = *p++;
        fd_buf[i] = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && j < (int)sizeof(port_buf) - 1) port_buf[j++] = *p++;
        port_buf[j] = 0;

        if (!parse_nonneg_int(fd_buf, &fd) || !parse_nonneg_int(port_buf, &port) || port < 1 || port > 65535) {
            print_string("Usage: sock bind <fd> <port>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        r = sock_bind(fd, (uint16_t)port);
        if (r > 0) print_string("SOCK: bind ok", -1, video, cursor, COLOR_LIGHT_GREEN);
        else print_string("SOCK: bind failed", -1, video, cursor, COLOR_LIGHT_RED);
    } else if (cmd[0] == 's' && cmd[1] == 'o' && cmd[2] == 'c' && cmd[3] == 'k' && cmd[4] == ' ' && cmd[5] == 's' && cmd[6] == 'e' && cmd[7] == 'n' && cmd[8] == 'd' && cmd[9] == ' ') {
        int fd = 0;
        int port = 0;
        uint8_t ip[4];
        char fd_buf[16];
        char ip_buf[20];
        char port_buf[16];
        const char* p = cmd + 10;
        int i = 0;
        int j = 0;
        int k = 0;
        int r;

        while (*p == ' ') p++;
        while (*p && *p != ' ' && i < (int)sizeof(fd_buf) - 1) fd_buf[i++] = *p++;
        fd_buf[i] = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && j < (int)sizeof(ip_buf) - 1) ip_buf[j++] = *p++;
        ip_buf[j] = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && k < (int)sizeof(port_buf) - 1) port_buf[k++] = *p++;
        port_buf[k] = 0;
        while (*p == ' ') p++;

        if (!parse_nonneg_int(fd_buf, &fd) || !parse_ipv4_text(ip_buf, ip) || !parse_nonneg_int(port_buf, &port) || port < 1 || port > 65535 || *p == 0) {
            print_string("Usage: sock send <fd> <a.b.c.d> <port> <text>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        {
            int payload_len = str_len(p);
            if (payload_len > 512) payload_len = 512;
            r = sock_sendto(fd, ip, (uint16_t)port, (const uint8_t*)p, payload_len);
        }

        if (r > 0) print_string("SOCK: send ok", -1, video, cursor, COLOR_LIGHT_GREEN);
        else if (r == -4) print_string("SOCK: target MAC unknown (run arp whohas first)", -1, video, cursor, COLOR_YELLOW);
        else print_string("SOCK: send failed", -1, video, cursor, COLOR_LIGHT_RED);
    } else if (cmd[0] == 's' && cmd[1] == 'o' && cmd[2] == 'c' && cmd[3] == 'k' && cmd[4] == ' ' && cmd[5] == 'r' && cmd[6] == 'e' && cmd[7] == 'c' && cmd[8] == 'v' && cmd[9] == ' ') {
        int fd = 0;
        int r;
        uint8_t src_ip[4];
        uint16_t src_port = 0;
        uint8_t payload[513];
        int payload_len = 0;
        char line[128];
        char value[24];

        if (!parse_nonneg_int(cmd + 10, &fd)) {
            print_string("Usage: sock recv <fd>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        r = sock_recvfrom(fd, src_ip, &src_port, payload, 512, &payload_len);
        if (r == 0) {
            print_string("SOCK: no data", -1, video, cursor, COLOR_YELLOW);
            return;
        }
        if (r < 0) {
            print_string("SOCK: recv failed", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (payload_len < 0) payload_len = 0;
        if (payload_len > 512) payload_len = 512;
        payload[payload_len] = 0;

        line[0] = 0;
        str_concat(line, "SOCK: from ");
        int_to_str(src_ip[0], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[1], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[2], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[3], value); str_concat(line, value);
        str_concat(line, ":");
        int_to_str((int)src_port, value); str_concat(line, value);
        str_concat(line, " len=");
        int_to_str(payload_len, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);

        for (int n = 0; n < payload_len; n++) {
            if (payload[n] < 32 || payload[n] > 126) payload[n] = '.';
        }
        print_string((const char*)payload, payload_len, video, cursor, COLOR_LIGHT_GRAY);
    } else if (cmd[0] == 's' && cmd[1] == 'o' && cmd[2] == 'c' && cmd[3] == 'k' && cmd[4] == ' ' && cmd[5] == 'c' && cmd[6] == 'l' && cmd[7] == 'o' && cmd[8] == 's' && cmd[9] == 'e' && cmd[10] == ' ') {
        int fd = 0;
        if (!parse_nonneg_int(cmd + 11, &fd)) {
            print_string("Usage: sock close <fd>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (sock_close(fd) > 0) print_string("SOCK: closed", -1, video, cursor, COLOR_LIGHT_GREEN);
        else print_string("SOCK: close failed", -1, video, cursor, COLOR_LIGHT_RED);
    } else if (mini_strcmp(cmd, "sock list") == 0) {
        int count = sock_get_count();
        char line[96];
        char value[24];

        if (count == 0) {
            print_string("SOCK: no open sockets", -1, video, cursor, COLOR_YELLOW);
            return;
        }

        for (int i = 0; i < count; i++) {
            SocketInfo info;
            if (!sock_get_info(i, &info)) continue;
            line[0] = 0;
            str_concat(line, "SOCK ");
            str_concat(line, info.type == SOCK_TYPE_UDP ? "UDP" : "?");
            str_concat(line, " local=");
            int_to_str((int)info.local_port, value); str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_CYAN);
        }
    } else if (mini_strcmp(cmd, "sock") == 0) {
        print_string("SOCK usage: sock open udp|bind|send|recv|close|list", -1, video, cursor, COLOR_YELLOW);
    } else if (mini_strcmp(cmd, "about") == 0) {
        handle_command(cmd, video, cursor, "about", "Smiggles OS is a lightweight operating system designed by Jules Miller and Vajra Vanukuri.", COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help") == 0) {
        print_string(
            "---Filesystem---\n"
            "touch file.txt - create file\n"
            "echo \"text\" > <file> - write to file\n"
            "mkdir <path> - create directory\n"
            "rm <path> - remove file\n"
            "rmdir <path> - remove directory\n"
            "cat file.txt - read file \n"
            "cp <location> <destination> - copy file\n"
            "mv <old> <new> - rename/move file\n"
            "grep <pattern> <file> - search in file\n"
            "edit <file> - text editor \n"
            "filesize <file> - show file size\n"
            "hexdump <file> - show hexdump of file\n"
            "chmod <file> - change file permissions with octal system\n",
            -1, video, cursor, COLOR_YELLOW);

        print_string(
            "---System commands---\n"
            "about - about Smiggles\n"
            "ver - version info\n"
            "uptime - system uptime\n"
            "neofetch - system info\n"
            "pciscan - detect RTL8139 on PCI\n"
            "rtltest - init RTL8139 and print MAC\n"
            "rtltx - send test Ethernet frame\n"
            "rtlrx - poll one received Ethernet frame\n"
            "arp setip <a.b.c.d> - set local IPv4\n"
            "arp whohas <a.b.c.d> - send ARP request\n"
            "arp poll - poll one frame and process ARP\n"
            "arp table - show ARP cache\n"
            "ip poll - poll and parse one IPv4 frame\n"
            "ip stats - show IPv4 parser counters\n"
            "ping <a.b.c.d> - send ICMP echo request\n"
            "icmp poll - process one ICMP packet\n"
            "icmp stats - show ICMP counters\n"
            "udp send <a.b.c.d> <port> <text> - send UDP datagram\n"
            "udp poll - unified poll and process one packet\n"
            "udp recv - read one queued UDP payload\n"
            "udp stats - show UDP counters\n"
            "udp listen <port>|off|show - filter rx by dst port\n"
            "net poll - unified poll (ARP/ICMP/UDP/TCP)\n"
            "tcp poll - unified poll and process one TCP packet\n"
            "tcp listen <port>|off|show - TCP handshake listener\n"
            "tcp stats - show TCP handshake counters\n"
            "tcp conns - list tracked TCP handshakes\n"
            "sock open udp - open UDP socket\n"
            "sock bind <fd> <port> - bind UDP socket\n"
            "sock send <fd> <ip> <port> <text> - send UDP via socket\n"
            "sock recv <fd> - receive UDP via bound socket\n"
            "sock close <fd> - close socket\n"
            "sock list - list open sockets\n"
            "basic - BASIC interpreter\n"
            "exec <file.bas> - run BASIC file\n"
            "fdtest <file> - fd syscall open/write/read test\n"
            "panic - show kernel panic screen\n"
            "halt - shutdown\n"
            "reboot - restart\n",
            -1, video, cursor, COLOR_LIGHT_CYAN);

        print_string(
            "---User authentication---\n"
            "whoami - view logged in user\n"
            "login - log in with username/password\n"
            "logout - log out\n"
            "edituser - edit account information\n"
            "---Admin-only commands---\n"
            "adduser - add new user"
            "deluser - delete user\n"
            "listusers - list all users\n"
            "edituser - edit any account\n"
            "chown <file> - change file owner",
            -1, video, cursor, COLOR_LIGHT_GREEN);

    } else if (mini_strcmp(cmd, "basic") == 0) {
        basic_repl(video, cursor);

    } else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c' && cmd[4] == ' ') {
        int start = 5;
        char filename[MAX_PATH_LENGTH];
        int fi = 0;

        while (cmd[start] == ' ') start++;
        while (cmd[start] && fi < MAX_PATH_LENGTH - 1) {
            filename[fi++] = cmd[start++];
        }
        while (fi > 0 && filename[fi - 1] == ' ') fi--;
        filename[fi] = 0;

        if (filename[0] == 0) {
            print_string("Usage: exec <file.bas>", -1, video, cursor, COLOR_LIGHT_RED);
        } else {
            basic_run_file(filename, video, cursor);
        }

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
    } else if (cmd[0] == 's' && cmd[1] == 'p' && cmd[2] == 'a' && cmd[3] == 'w' && cmd[4] == 'n' && cmd[5] == ' ') {
        handle_spawn_command(cmd + 6, video, cursor);
    } else if (mini_strcmp(cmd, "ps") == 0) {
        handle_ps_command(video, cursor);
    } else if (cmd[0] == 'k' && cmd[1] == 'i' && cmd[2] == 'l' && cmd[3] == 'l' && cmd[4] == ' ') {
        handle_kill_command(cmd + 5, video, cursor);
    } else if (cmd[0] == 'w' && cmd[1] == 'a' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == ' ') {
        handle_wait_command(cmd + 5, video, cursor);
    } else if (mini_strcmp(cmd, "fdtest") == 0) {
        handle_fdtest_command("", video, cursor);
    } else if (cmd[0] == 'f' && cmd[1] == 'd' && cmd[2] == 't' && cmd[3] == 'e' && cmd[4] == 's' && cmd[5] == 't' && cmd[6] == ' ') {
        handle_fdtest_command(cmd + 7, video, cursor);
    } else if (mini_strcmp(cmd, "syscalltest") == 0) {
        handle_syscalltest_command(video, cursor);
    } else if (mini_strcmp(cmd, "halt") == 0) {
        handle_halt_command(video, cursor);
    } else if (mini_strcmp(cmd, "panic") == 0) {
        handle_panic_command();
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
        "echo", "edit", "tree", "grep", "clear", "cls", "help", "time", "ping", "exec",
        "udp", "tcp", "net", "sock", "about", "ver", "panic", "halt", "reboot", "history", "df", "fscheck", "free", "uptime", "filesize", "neofetch", "basic", "syscalltest", "fdtest", "spawn", "ps", "kill", "wait"
    };
    int cmd_count = (int)(sizeof(commands) / sizeof(commands[0]));
    
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