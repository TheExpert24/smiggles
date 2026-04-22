#include "kernel.h"
#include <stddef.h>
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

extern int fs_runtime_ensure_newfs(void);
extern int path_resolve(const char* path, FInode* inode_out);
extern int disk_write_inode(uint32_t inode_num, const FInode* inode);

static char newfs_cwd[MAX_PATH_LENGTH] = "/";

static void shell_ensure_newfs_cwd(void) {
    if (newfs_cwd[0] == 0 || newfs_cwd[0] != '/') {
        str_copy(newfs_cwd, "/", MAX_PATH_LENGTH);
        return;
    }
    newfs_cwd[MAX_PATH_LENGTH - 1] = 0;
}

static int shell_trim_path(const char* raw, char* out, int out_max) {
    int start = 0;
    int end;
    int len = 0;
    if (!raw || !out || out_max <= 1) return 0;
    while (raw[start] == ' ') start++;
    end = str_len(raw);
    while (end > start && raw[end - 1] == ' ') end--;
    if (end <= start) return 0;
    while (start < end && len < out_max - 1) out[len++] = raw[start++];
    out[len] = 0;
    return len > 0;
}

static void shell_normalize_abs_path(const char* abs_path, char* out_path) {
    char parts[32][MAX_NAME_LENGTH];
    int part_count = 0;
    int i = 0;

    while (abs_path && abs_path[i]) {
        while (abs_path[i] == '/') i++;
        if (!abs_path[i]) break;

        char part[MAX_NAME_LENGTH];
        int pi = 0;
        while (abs_path[i] && abs_path[i] != '/' && pi < MAX_NAME_LENGTH - 1) {
            part[pi++] = abs_path[i++];
        }
        part[pi] = 0;

        if (str_equal(part, ".")) continue;
        if (str_equal(part, "..")) {
            if (part_count > 0) part_count--;
            continue;
        }

        if (part_count < 32) {
            str_copy(parts[part_count], part, MAX_NAME_LENGTH);
            part_count++;
        }
    }

    out_path[0] = '/';
    out_path[1] = 0;
    for (int p = 0; p < part_count; p++) {
        if (!str_equal(out_path, "/")) str_concat(out_path, "/");
        str_concat(out_path, parts[p]);
    }
}

static int shell_resolve_newfs_path(const char* raw_path, char* out_path) {
    char trimmed[MAX_PATH_LENGTH];
    char combined[MAX_PATH_LENGTH];

    shell_ensure_newfs_cwd();
    if (!shell_trim_path(raw_path, trimmed, (int)sizeof(trimmed))) return 0;

    if (trimmed[0] == '/') {
        str_copy(combined, trimmed, (int)sizeof(combined));
    } else {
        if (str_equal(newfs_cwd, "/")) {
            combined[0] = '/';
            combined[1] = 0;
            str_concat(combined, trimmed);
        } else {
            str_copy(combined, newfs_cwd, (int)sizeof(combined));
            str_concat(combined, "/");
            str_concat(combined, trimmed);
        }
    }

    shell_normalize_abs_path(combined, out_path);
    return 1;
}

static void handle_filesize_command(const char* filename, char* video, int* cursor) {
    char path[MAX_PATH_LENGTH];
    if (!filename || !shell_resolve_newfs_path(filename, path)) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int fd = (int)syscall_invoke2(SYS_OPEN, (unsigned int)path, (unsigned int)FS_O_READ);
    if (fd < 0) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int size = 0;
    char buf_chunk[128];
    while (1) {
        int n = (int)syscall_invoke3(SYS_READ, (unsigned int)fd, (unsigned int)buf_chunk, (unsigned int)sizeof(buf_chunk));
        if (n < 0) {
            syscall_invoke1(SYS_CLOSE, (unsigned int)fd);
            print_string("Cannot read file", 16, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (n == 0) break;
        size += n;
    }
    syscall_invoke1(SYS_CLOSE, (unsigned int)fd);

    char buf[64];
    str_copy(buf, "Size: ", 64);
    char temp[16];
    int_to_str(size, temp);
    str_concat(buf, temp);
    str_concat(buf, " bytes");
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

// --- Global Variables ---
char history[10][64];
int history_count = 0;
static int udpecho_fd = -1;
static uint16_t udpecho_port = 0;
static int logger_ready = 0;
static const char* shell_stdin_data = 0;
static int shell_stdin_len = 0;
static int shell_stdin_active = 0;
static int shell_save_dir_idx = -1;

static void ensure_logger_ready(void) {
    if (!logger_ready) {
        logger_init();
        logger_ready = 1;
    }
}

// --- Dynamic Groups ---
Group group_table[MAX_GROUPS] = {
    {"admin", 0x01, 1},
    {"users", 0x02, 1},
    {"guests", 0x04, 1},
    {"net", 0x08, 1},
    {"dev", 0x10, 1},
};
int group_count = 5;

// --- Shell Variables ---
#define MAX_VARS 32
#define MAX_VAR_NAME 32
#define MAX_VAR_VALUE 128
// --- Script Arguments ---
#define MAX_SCRIPT_ARGS 9
static char script_args[MAX_SCRIPT_ARGS][MAX_VAR_VALUE];
static int script_argc = 0;
typedef struct {
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
    int used;
} ShellVar;
static ShellVar shell_vars[MAX_VARS];

// Helper: find variable index by name
static int find_var(const char* name) {
    for (int i = 0; i < MAX_VARS; i++) {
        if (shell_vars[i].used && str_equal(shell_vars[i].name, name)) return i;
    }
    return -1;
}

// Helper: set variable (create or update)
static void set_var(const char* name, const char* value) {
    int idx = find_var(name);
    if (idx == -1) {
        for (int i = 0; i < MAX_VARS; i++) {
            if (!shell_vars[i].used) {
                str_copy(shell_vars[i].name, name, MAX_VAR_NAME);
                str_copy(shell_vars[i].value, value, MAX_VAR_VALUE);
                shell_vars[i].used = 1;
                return;
            }
        }
    } else {
        str_copy(shell_vars[idx].value, value, MAX_VAR_VALUE);
    }
}

// Helper: get variable value (returns pointer or NULL)
static const char* get_var(const char* name) {
    int idx = find_var(name);
    if (idx != -1) return shell_vars[idx].value;
    return NULL;
}

// Substitute $VAR in src into dest (dest must be large enough)
static void substitute_vars(const char* src, char* dest, int max_len) {
    int si = 0, di = 0;
    while (src[si] && di < max_len - 1) {
        if (src[si] == '$') {
            si++;
            // Check for $1-$9 (script arguments)
            if (src[si] >= '1' && src[si] <= '9') {
                int arg_idx = src[si] - '1';
                si++;
                if (arg_idx < script_argc) {
                    const char* val = script_args[arg_idx];
                    for (int vj = 0; val[vj] && di < max_len - 1; vj++) dest[di++] = val[vj];
                }
            } else {
                char varname[MAX_VAR_NAME];
                int vi = 0;
                while ((src[si] >= 'A' && src[si] <= 'Z') || (src[si] >= 'a' && src[si] <= 'z') || (src[si] >= '0' && src[si] <= '9') || src[si] == '_') {
                    if (vi < MAX_VAR_NAME - 1) varname[vi++] = src[si];
                    si++;
                }
                varname[vi] = 0;
                const char* val = get_var(varname);
                if (val) {
                    for (int vj = 0; val[vj] && di < max_len - 1; vj++) dest[di++] = val[vj];
                }
            }
        } else {
            dest[di++] = src[si++];
        }
    }
    dest[di] = 0;
}

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

    unsigned char hash[HASH_SIZE];
    hash_password(password, hash);
    for (int i = 0; i < user_count; i++) {
        if (mini_strcmp(username, user_table[i].username) == 0) {
            int match = 1;
            for (int j = 0; j < HASH_SIZE; j++) {
                if (user_table[i].password_hash[j] != hash[j]) { match = 0; break; }
            }
            if (match) {
                current_user_idx = i;
                {
                    char line[LOGGER_MESSAGE_MAX];
                    line[0] = 0;
                    str_concat(line, "auth: login success user=");
                    str_concat(line, username);
                    log_write(LOG_LEVEL_INFO, line);
                }
                print_string("Login successful!", -1, video, cursor, COLOR_LIGHT_GREEN);
                return;
            }
        }
    }
    {
        char line[LOGGER_MESSAGE_MAX];
        line[0] = 0;
        str_concat(line, "auth: login failed user=");
        str_concat(line, username);
        log_write(LOG_LEVEL_WARN, line);
    }
    print_string("Login failed.", -1, video, cursor, COLOR_LIGHT_RED);
}

static int shell_read_file_content(const char* path, char* out, int max_len);
static int shell_write_file_content(const char* path, const char* data, int len, int append);
static int shell_resolve_required_path(const char* raw_path, char* out_path, const char* usage_msg, char* video, int* cursor);

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

typedef struct {
    const char* name;
    int base_offset_minutes;
    int observes_dst;
    const char* standard_abbr;
    const char* daylight_abbr;
} TimeZoneRule;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} TimeDate;

static const char timezone_config_path[] = "/timezone";
static const TimeZoneRule timezone_rules[] = {
    {"UTC", 0, 0, "UTC", "UTC"},
    {"GMT", 0, 0, "GMT", "GMT"},
    {"ET", -300, 1, "EST", "EDT"},
    {"CT", -360, 1, "CST", "CDT"},
    {"MT", -420, 1, "MST", "MDT"},
    {"PT", -480, 1, "PST", "PDT"},
};

static char current_timezone_name[8] = "UTC";

static char tz_upper_char(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static void tz_normalize_name(const char* src, char* dst, int max_len) {
    int di = 0;
    if (!src || !dst || max_len <= 0) return;
    while (src[di] && di < max_len - 1) {
        dst[di] = tz_upper_char(src[di]);
        di++;
    }
    dst[di] = 0;
}

static int time_find_timezone_rule(const char* tz_name) {
    char normalized[8];
    if (!tz_name || !tz_name[0]) return -1;
    tz_normalize_name(tz_name, normalized, (int)sizeof(normalized));

    if (mini_strcmp(normalized, "EST") == 0 || mini_strcmp(normalized, "EDT") == 0) str_copy(normalized, "ET", (int)sizeof(normalized));
    if (mini_strcmp(normalized, "CST") == 0 || mini_strcmp(normalized, "CDT") == 0) str_copy(normalized, "CT", (int)sizeof(normalized));
    if (mini_strcmp(normalized, "MST") == 0 || mini_strcmp(normalized, "MDT") == 0) str_copy(normalized, "MT", (int)sizeof(normalized));
    if (mini_strcmp(normalized, "PST") == 0 || mini_strcmp(normalized, "PDT") == 0) str_copy(normalized, "PT", (int)sizeof(normalized));

    for (int i = 0; i < (int)(sizeof(timezone_rules) / sizeof(timezone_rules[0])); i++) {
        if (mini_strcmp(normalized, timezone_rules[i].name) == 0) return i;
    }
    return -1;
}

int time_set_timezone(const char* tz_name) {
    int rule_index = time_find_timezone_rule(tz_name);
    if (rule_index < 0) return 0;

    str_copy(current_timezone_name, timezone_rules[rule_index].name, (int)sizeof(current_timezone_name));
    return 1;
}

const char* time_get_timezone_name(void) {
    return current_timezone_name;
}

static int time_is_leap_year(int year) {
    if ((year % 400) == 0) return 1;
    if ((year % 100) == 0) return 0;
    return (year % 4) == 0;
}

static int time_days_in_month(int year, int month) {
    static const int days_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 30;
    if (month == 2 && time_is_leap_year(year)) return 29;
    return days_norm[month - 1];
}

static int time_weekday_0_sun(int year, int month, int day) {
    static const int table[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year -= 1;
    return (year + year/4 - year/100 + year/400 + table[month - 1] + day) % 7;
}

static int time_nth_weekday_of_month(int year, int month, int weekday, int nth) {
    int first_wday = time_weekday_0_sun(year, month, 1);
    int first_match = 1 + ((7 + weekday - first_wday) % 7);
    return first_match + (nth - 1) * 7;
}

static void time_add_days(TimeDate* dt, int delta_days) {
    if (!dt) return;
    while (delta_days > 0) {
        int dim = time_days_in_month(dt->year, dt->month);
        dt->day++;
        if (dt->day > dim) {
            dt->day = 1;
            dt->month++;
            if (dt->month > 12) {
                dt->month = 1;
                dt->year++;
            }
        }
        delta_days--;
    }
    while (delta_days < 0) {
        dt->day--;
        if (dt->day < 1) {
            dt->month--;
            if (dt->month < 1) {
                dt->month = 12;
                dt->year--;
            }
            dt->day = time_days_in_month(dt->year, dt->month);
        }
        delta_days++;
    }
}

static void time_add_minutes(TimeDate* dt, int delta_minutes) {
    int total_minutes;
    int day_delta = 0;
    if (!dt) return;

    total_minutes = dt->hour * 60 + dt->minute + delta_minutes;
    while (total_minutes < 0) {
        total_minutes += 24 * 60;
        day_delta--;
    }
    while (total_minutes >= 24 * 60) {
        total_minutes -= 24 * 60;
        day_delta++;
    }

    dt->hour = total_minutes / 60;
    dt->minute = total_minutes % 60;
    time_add_days(dt, day_delta);
}

static void time_read_cmos_datetime(TimeDate* out_dt) {
    unsigned char year = cmos_read(0x09);
    unsigned char month = cmos_read(0x08);
    unsigned char day = cmos_read(0x07);
    unsigned char hour = cmos_read(0x04);
    unsigned char min = cmos_read(0x02);
    unsigned char sec = cmos_read(0x00);

    if (!out_dt) return;

    out_dt->year = 2000 + (int)bcd_to_bin(year);
    out_dt->month = (int)bcd_to_bin(month);
    out_dt->day = (int)bcd_to_bin(day);
    out_dt->hour = (int)bcd_to_bin(hour);
    out_dt->minute = (int)bcd_to_bin(min);
    out_dt->second = (int)bcd_to_bin(sec);
}

static int time_is_us_dst_active(const TimeDate* local_standard) {
    int start_day;
    int end_day;

    if (!local_standard) return 0;
    if (local_standard->month < 3 || local_standard->month > 11) return 0;
    if (local_standard->month > 3 && local_standard->month < 11) return 1;

    if (local_standard->month == 3) {
        start_day = time_nth_weekday_of_month(local_standard->year, 3, 0, 2); // second Sunday
        if (local_standard->day > start_day) return 1;
        if (local_standard->day < start_day) return 0;
        return local_standard->hour >= 2;
    }

    end_day = time_nth_weekday_of_month(local_standard->year, 11, 0, 1); // first Sunday
    if (local_standard->day < end_day) return 1;
    if (local_standard->day > end_day) return 0;
    return local_standard->hour < 2;
}

static int time_is_current_zone_dst(void) {
    int rule_index = time_find_timezone_rule(current_timezone_name);
    TimeDate dt;

    if (rule_index < 0) return 0;
    if (!timezone_rules[rule_index].observes_dst) return 0;

    time_read_cmos_datetime(&dt);
    time_add_minutes(&dt, timezone_rules[rule_index].base_offset_minutes);
    return time_is_us_dst_active(&dt);
}

static const char* time_get_display_timezone_abbr(void) {
    int rule_index = time_find_timezone_rule(current_timezone_name);
    if (rule_index < 0) return "UTC";
    if (!timezone_rules[rule_index].observes_dst) return timezone_rules[rule_index].standard_abbr;
    return time_is_current_zone_dst() ? timezone_rules[rule_index].daylight_abbr : timezone_rules[rule_index].standard_abbr;
}

static int time_get_timezone_offset_minutes(void) {
    int rule_index = time_find_timezone_rule(current_timezone_name);
    if (rule_index < 0) return 0;
    if (timezone_rules[rule_index].observes_dst && time_is_current_zone_dst()) {
        return timezone_rules[rule_index].base_offset_minutes + 60;
    }
    return timezone_rules[rule_index].base_offset_minutes;
}

static void time_seconds_to_hms(int seconds_since_midnight, char* buf) {
    int hour24 = 0;
    int hour12 = 0;
    int min = 0;
    int sec = 0;
    const char* meridiem = "AM";

    while (seconds_since_midnight < 0) seconds_since_midnight += 24 * 60 * 60;
    while (seconds_since_midnight >= 24 * 60 * 60) seconds_since_midnight -= 24 * 60 * 60;

    hour24 = seconds_since_midnight / 3600;
    seconds_since_midnight %= 3600;
    min = seconds_since_midnight / 60;
    sec = seconds_since_midnight % 60;

    if (hour24 >= 12) meridiem = "PM";
    hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;

    buf[0] = '0' + (hour12 / 10);
    buf[1] = '0' + (hour12 % 10);
    buf[2] = ':';
    buf[3] = '0' + (min / 10);
    buf[4] = '0' + (min % 10);
    buf[5] = ':';
    buf[6] = '0' + (sec / 10);
    buf[7] = '0' + (sec % 10);
    buf[8] = ' ';
    buf[9] = meridiem[0];
    buf[10] = meridiem[1];
    buf[11] = 0;
}

void get_time_string(char* buf) {
    TimeDate dt;
    int local_seconds;
    int adjusted_seconds;

    time_read_cmos_datetime(&dt);
    local_seconds = dt.hour * 3600 + dt.minute * 60 + dt.second;
    adjusted_seconds = local_seconds + (time_get_timezone_offset_minutes() * 60);
    time_seconds_to_hms(adjusted_seconds, buf);
}

void time_settings_save(void) {
    char value[32];

    value[0] = 0;
    str_concat(value, current_timezone_name);

    shell_write_file_content(timezone_config_path, value, str_len(value), 0);
}

void time_settings_load(void) {
    char config[32];
    int len = shell_read_file_content(timezone_config_path, config, (int)sizeof(config));

    if (len > 0) {
        char name[8];
        int i = 0;
        int n = 0;

        while (config[i] == ' ') i++;
        while (config[i] && config[i] != ' ' && n < (int)sizeof(name) - 1) {
            name[n++] = config[i++];
        }
        name[n] = 0;
        while (config[i] == ' ') i++;
        if (time_set_timezone(name)) {
            return;
        }
    }

    time_set_timezone("UTC");
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

static int shell_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void shell_trim_copy(char* dst, const char* src, int max_len) {
    int start = 0;
    int end = str_len(src);
    int di = 0;

    while (src[start] && shell_is_space(src[start])) start++;
    while (end > start && shell_is_space(src[end - 1])) end--;

    for (int i = start; i < end && di < max_len - 1; i++) {
        dst[di++] = src[i];
    }
    dst[di] = 0;
}

static int shell_find_unquoted(const char* s, char needle) {
    int in_quote = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '"') in_quote = !in_quote;
        if (!in_quote && s[i] == needle) return i;
    }
    return -1;
}

static int shell_read_file_content(const char* path, char* out, int max_len) {
    int idx;
    int copy_len;
    char resolved[MAX_PATH_LENGTH];

    if (!path || !out || max_len <= 0) return -1;
    if (!fs_runtime_ensure_newfs()) return -1;
    if (!shell_resolve_newfs_path(path, resolved)) return -1;

    idx = vfs_open(resolved, FS_O_READ);
    if (idx < 0) return -1;

    copy_len = vfs_read(idx, out, max_len - 1);
    vfs_close(idx);
    if (copy_len < 0) return -1;
    out[copy_len] = 0;
    return copy_len;
}

static int shell_write_file_content(const char* path, const char* data, int len, int append) {
    int flags = FS_O_WRITE | FS_O_CREATE;
    int fd;
    char resolved[MAX_PATH_LENGTH];

    if (!path || !data || len < 0) return -1;
    if (!fs_runtime_ensure_newfs()) return -1;
    if (!shell_resolve_newfs_path(path, resolved)) return -1;
    flags |= append ? FS_O_APPEND : FS_O_TRUNC;

    fd = vfs_open(resolved, flags);
    if (fd < 0) return -1;

    if (len > 0 && vfs_write(fd, data, len) != len) {
        vfs_close(fd);
        return -1;
    }

    vfs_close(fd);
    return 0;
}

static int shell_starts_with_echo(const char* cmd) {
    return cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == ' ';
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



// --- Command Handlers ---
static void handle_command(const char* cmd, char* video, int* cursor, const char* input, const char* output, unsigned char color) {
    if (mini_strcmp(cmd, input) == 0) {
        print_string(output, -1, video, cursor, color);
    }
}

static void handle_ls_command(char* video, int* cursor, unsigned char color_unused) {
    (void)color_unused;
    if (!fs_runtime_ensure_newfs()) {
        print_string("Filesystem unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    shell_ensure_newfs_cwd();
    DirectoryEntry entries[128];
    int count = vfs_readdir(newfs_cwd, entries, 128);
    if (count < 0) {
        str_copy(newfs_cwd, "/", MAX_PATH_LENGTH);
        count = vfs_readdir(newfs_cwd, entries, 128);
    }
    if (count < 0) {
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int printed = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].inode == 0) continue;
        if ((entries[i].name_len == 1 && entries[i].name[0] == '.') ||
            (entries[i].name_len == 2 && entries[i].name[0] == '.' && entries[i].name[1] == '.')) {
            continue;
        }

        char name_buf[253];
        int nlen = entries[i].name_len;
        if (nlen < 0) nlen = 0;
        if (nlen > 252) nlen = 252;
        for (int j = 0; j < nlen; j++) name_buf[j] = entries[i].name[j];
        name_buf[nlen] = 0;

        if (entries[i].file_type == DIRENT_TYPE_DIR) {
            print_string(name_buf, -1, video, cursor, COLOR_LIGHT_CYAN);
            print_string_sameline("/", 1, video, cursor, COLOR_LIGHT_CYAN);
        } else {
            print_string(name_buf, -1, video, cursor, COLOR_LIGHT_CYAN);
        }
        printed++;
    }

    if (printed == 0) {
        print_string("(empty)", 7, video, cursor, COLOR_LIGHT_CYAN);
    }
}

static void handle_lsall_command(char* video, int* cursor) {
    shell_ensure_newfs_cwd();
    DirectoryEntry entries[128];
    int count = vfs_readdir(newfs_cwd, entries, 128);
    if (count < 0) {
        print_string("Directory not found", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (count == 0) {
        print_string("(empty)", 7, video, cursor, COLOR_LIGHT_CYAN);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (entries[i].inode == 0) continue;
        char name_buf[253];
        int nlen = entries[i].name_len;
        if (nlen < 0) nlen = 0;
        if (nlen > 252) nlen = 252;
        for (int j = 0; j < nlen; j++) name_buf[j] = entries[i].name[j];
        name_buf[nlen] = 0;

        print_string(name_buf, -1, video, cursor, COLOR_LIGHT_CYAN);
        if (entries[i].file_type == DIRENT_TYPE_DIR) {
            print_string_sameline("/", 1, video, cursor, COLOR_LIGHT_CYAN);
        }
    }
}

static void handle_cat_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    (void)color_unused;
    if ((!filename || filename[0] == 0) && shell_stdin_active && shell_stdin_data && shell_stdin_len > 0) {
        print_string(shell_stdin_data, shell_stdin_len, video, cursor, COLOR_LIGHT_GRAY);
        return;
    }
    if (!filename || filename[0] == 0) {
        print_string("Usage: cat <file>", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    char path[MAX_PATH_LENGTH];
    if (!shell_resolve_newfs_path(filename, path)) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int fd = (int)syscall_invoke2(SYS_OPEN, (unsigned int)path, (unsigned int)FS_O_READ);
    if (fd < 0) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    char chunk[128];
    int total = 0;
    while (1) {
        int n = (int)syscall_invoke3(SYS_READ, (unsigned int)fd, (unsigned int)chunk, (unsigned int)sizeof(chunk));
        if (n < 0) {
            syscall_invoke1(SYS_CLOSE, (unsigned int)fd);
            print_string("Cannot read file", 16, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (n == 0) break;
        total += n;
        print_string(chunk, n, video, cursor, COLOR_LIGHT_GRAY);
    }

    syscall_invoke1(SYS_CLOSE, (unsigned int)fd);
    if (total == 0) {
        print_string("File is empty", 13, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_echo_command(const char* text, const char* filename, char* video, int* cursor, unsigned char color_unused) {
    (void)color_unused;
    if (!filename || filename[0] == 0) {
        print_string("Cannot write file", 17, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    char path[MAX_PATH_LENGTH];
    int start = 0;
    int end = 0;
    while (filename[start] == ' ') start++;
    while (filename[start + end] && end < MAX_PATH_LENGTH - 1) {
        path[end] = filename[start + end];
        end++;
    }
    while (end > 0 && path[end - 1] == ' ') end--;
    path[end] = 0;
    if (path[0] == 0) {
        print_string("Cannot write file", 17, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int len = text ? str_len(text) : 0;
    if (shell_write_file_content(path, text ? text : "", len, 0) != 0) {
        print_string("Cannot write file", 17, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    print_string("OK", 2, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_rm_command(const char* filename, char* video, int* cursor, unsigned char color_unused) {
    (void)color_unused;
    char path[MAX_PATH_LENGTH];
    if (!shell_resolve_newfs_path(filename, path)) {
        print_string("File not found or cannot remove root", 37, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int result = vfs_unlink(path);
    if (result < 0) {
        print_string("File not found or cannot remove root", 37, video, cursor, COLOR_LIGHT_RED);
    } else {
        print_string("Removed", 7, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_time_command(char* video, int* cursor, unsigned char color) {
    char timebuf[12];
    get_time_string(timebuf);
    print_string(timebuf, -1, video, cursor, color);
    print_string_sameline(" ", 1, video, cursor, color);
    print_string_sameline(time_get_display_timezone_abbr(), -1, video, cursor, color);
}

static void handle_timezone_command(const char* args, char* video, int* cursor) {
    char tz_name[8];
    char subcmd[16];
    int i = 0;
    int n = 0;

    while (args[i] == ' ') i++;
    while (args[i] && args[i] != ' ' && n < (int)sizeof(tz_name) - 1) {
        tz_name[n++] = args[i++];
    }
    tz_name[n] = 0;
    tz_normalize_name(tz_name, tz_name, (int)sizeof(tz_name));

    if (tz_name[0] == 0) {
        char buf[64];
        int offset_minutes = time_get_timezone_offset_minutes();
        buf[0] = 0;
        str_concat(buf, "TZ: ");
        str_concat(buf, time_get_timezone_name());
        str_concat(buf, " (");
        int_to_str(offset_minutes, buf + str_len(buf));
        str_concat(buf, " min)");
        print_string(buf, -1, video, cursor, COLOR_LIGHT_CYAN);
        return;
    }

    if (mini_strcmp(tz_name, "SHOW") == 0) {
        while (args[i] == ' ') i++;
        n = 0;
        while (args[i] && args[i] != ' ' && n < (int)sizeof(subcmd) - 1) {
            subcmd[n++] = args[i++];
        }
        subcmd[n] = 0;
        tz_normalize_name(subcmd, subcmd, (int)sizeof(subcmd));

        if (subcmd[0] == 0) {
            char buf[64];
            int offset_minutes = time_get_timezone_offset_minutes();
            buf[0] = 0;
            str_concat(buf, "TZ: ");
            str_concat(buf, time_get_timezone_name());
            str_concat(buf, " (");
            int_to_str(offset_minutes, buf + str_len(buf));
            str_concat(buf, " min)");
            print_string(buf, -1, video, cursor, COLOR_LIGHT_CYAN);
            return;
        }

        if (mini_strcmp(subcmd, "TIMEZONES") == 0 || mini_strcmp(subcmd, "ZONES") == 0) {
            print_string("Available zones:", -1, video, cursor, COLOR_LIGHT_CYAN);
            print_string("  UTC", -1, video, cursor, COLOR_LIGHT_GREEN);
            print_string("  GMT", -1, video, cursor, COLOR_LIGHT_GREEN);
            print_string("  ET (auto EST/EDT)", -1, video, cursor, COLOR_LIGHT_GREEN);
            print_string("  CT (auto CST/CDT)", -1, video, cursor, COLOR_LIGHT_GREEN);
            print_string("  MT (auto MST/MDT)", -1, video, cursor, COLOR_LIGHT_GREEN);
            print_string("  PT (auto PST/PDT)", -1, video, cursor, COLOR_LIGHT_GREEN);
            print_string("Aliases accepted: EST, EDT, CST, CDT, MST, MDT, PST, PDT", -1, video, cursor, COLOR_YELLOW);
            return;
        }

        print_string("Usage: tz show [timezones]", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    if (mini_strcmp(tz_name, "SET") == 0) {
        while (args[i] == ' ') i++;
        n = 0;
        while (args[i] && args[i] != ' ' && n < (int)sizeof(tz_name) - 1) {
            tz_name[n++] = args[i++];
        }
        tz_name[n] = 0;
        tz_normalize_name(tz_name, tz_name, (int)sizeof(tz_name));
        if (tz_name[0] == 0) {
            print_string("Usage: tz set <UTC|GMT|ET|CT|MT|PT>", -1, video, cursor, COLOR_YELLOW);
            return;
        }
        if (!time_set_timezone(tz_name)) {
            print_string("Unknown timezone", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        time_settings_save();
        print_string("Timezone updated", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    // Allow shorthand: tz EST
    if (time_set_timezone(tz_name)) {
        time_settings_save();
        print_string("Timezone updated", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    print_string("Usage: tz [show|show timezones|set <zone>]", -1, video, cursor, COLOR_YELLOW);
}

void handle_clear_command(char* video, int* cursor) {
    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }
    *cursor = 0;
}

static void handle_mv_command(const char* oldname, const char* newname, char* video, int* cursor) {
    char src_path[MAX_PATH_LENGTH];
    char dst_path[MAX_PATH_LENGTH];
    char content[MAX_FILE_CONTENT];
    int content_len;

    if (!shell_resolve_newfs_path(oldname, src_path) || !shell_resolve_newfs_path(newname, dst_path)) {
        print_string("Source not found", 16, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    content_len = shell_read_file_content(src_path, content, (int)sizeof(content));
    if (content_len < 0) {
        print_string("Source not found", 16, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (shell_write_file_content(dst_path, content, content_len, 0) < 0) {
        print_string("Cannot rename file", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    vfs_unlink(src_path);
    print_string("Renamed", 7, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_mkdir_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    (void)color_unused;
    char path[MAX_PATH_LENGTH];
    if (!shell_resolve_newfs_path(dirname, path)) {
        print_string("Parent directory not found", 26, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int result = vfs_mkdir(path);
    if (result < 0) {
        print_string("Parent directory not found", 26, video, cursor, COLOR_LIGHT_RED);
    } else {
        print_string("Directory created", 17, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_cd_command(const char* dirname, char* video, int* cursor, unsigned char color_unused) {
    (void)color_unused;
    if (!fs_runtime_ensure_newfs()) {
        print_string("Filesystem unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    shell_ensure_newfs_cwd();
    char resolved[MAX_PATH_LENGTH];
    FInode inode;

    if (!dirname || str_equal(dirname, "")) {
        str_copy(newfs_cwd, "/", MAX_PATH_LENGTH);
        print_string("Changed to /", 12, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    if (!shell_resolve_newfs_path(dirname, resolved)) {
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (vfs_stat(resolved, &inode) < 0) {
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (!(inode.mode & INODE_MODE_DIR)) {
        print_string("Not a directory", 15, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    str_copy(newfs_cwd, resolved, MAX_PATH_LENGTH);
    print_string("Changed to: ", -1, video, cursor, COLOR_LIGHT_GREEN);
    print_string_sameline(newfs_cwd, -1, video, cursor, COLOR_LIGHT_GREEN);
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
    if (is_recursive) {
        print_string("rmdir -r not supported in newfs mode", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    char resolved[MAX_PATH_LENGTH];
    if (!shell_resolve_newfs_path(path, resolved)) {
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int result = vfs_unlink(resolved);
    if (result < 0) {
        print_string("Directory not found", 19, video, cursor, COLOR_LIGHT_RED);
    } else {
        print_string("Directory removed", 17, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_free_command(char* video, int* cursor) {
    FSuperblock sb;
    char buf[80] = "Inodes used: ";
    char temp[12];

    if (disk_read_superblock(&sb) != 0 || sb.total_inodes == 0) {
        print_string("Filesystem stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int used_inodes = (int)(sb.total_inodes - sb.free_inodes);
    int_to_str(used_inodes, temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str((int)sb.total_inodes, temp);
    str_concat(buf, temp);
    str_concat(buf, " | Blocks used: ");
    int_to_str((int)(sb.total_blocks - sb.free_blocks), temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str((int)sb.total_blocks, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_df_command(char* video, int* cursor) {
    FSuperblock sb;
    char buf[96] = "Disk blocks used: ";
    char temp[12];

    if (disk_read_superblock(&sb) != 0 || sb.total_blocks == 0) {
        print_string("Filesystem stats unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int_to_str((int)(sb.total_blocks - sb.free_blocks), temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str((int)sb.total_blocks, temp);
    str_concat(buf, temp);
    str_concat(buf, " (4KB blocks)");
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_fscheck_command(char* video, int* cursor) {
    char buf[96];
    char temp[16];
    FSuperblock sb;

    if (!fs_runtime_ensure_newfs()) {
        print_string("FS runtime: unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    print_string("FS runtime: ready", -1, video, cursor, COLOR_LIGHT_GREEN);

    if (disk_read_superblock(&sb) != 0) {
        print_string("FS superblock: read failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    buf[0] = 0;
    str_concat(buf, "Inodes ");
    int_to_str((int)(sb.total_inodes - sb.free_inodes), temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str((int)sb.total_inodes, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);

    buf[0] = 0;
    str_concat(buf, "Blocks ");
    int_to_str((int)(sb.total_blocks - sb.free_blocks), temp);
    str_concat(buf, temp);
    str_concat(buf, "/");
    int_to_str((int)sb.total_blocks, temp);
    str_concat(buf, temp);
    print_string(buf, -1, video, cursor, COLOR_LIGHT_GRAY);
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

static unsigned char log_level_color(int level) {
    if (level == LOG_LEVEL_DEBUG) return COLOR_DARK_GRAY;
    if (level == LOG_LEVEL_INFO) return COLOR_LIGHT_CYAN;
    if (level == LOG_LEVEL_WARN) return COLOR_YELLOW;
    if (level == LOG_LEVEL_ERROR) return COLOR_LIGHT_RED;
    return COLOR_WHITE;
}

static int log_count_safe_probe(void) {
    int count = 0;
    LogEntry scratch;

    while (count < 256) {
        if (!log_get_entry(count, &scratch)) break;
        count++;
    }

    return count;
}

static int parse_log_level(const char* text, int* out_level) {
    int numeric = 0;

    if (!text || !out_level) return 0;

    if (mini_strcmp(text, "debug") == 0) {
        *out_level = LOG_LEVEL_DEBUG;
        return 1;
    }
    if (mini_strcmp(text, "info") == 0) {
        *out_level = LOG_LEVEL_INFO;
        return 1;
    }
    if (mini_strcmp(text, "warn") == 0 || mini_strcmp(text, "warning") == 0) {
        *out_level = LOG_LEVEL_WARN;
        return 1;
    }
    if (mini_strcmp(text, "error") == 0) {
        *out_level = LOG_LEVEL_ERROR;
        return 1;
    }

    if (parse_nonneg_int(text, &numeric) && numeric >= LOG_LEVEL_DEBUG && numeric <= LOG_LEVEL_ERROR) {
        *out_level = numeric;
        return 1;
    }

    return 0;
}

static void handle_log_show_command(const char* args, char* video, int* cursor) {
    int requested = 20;
    int total = log_count_safe_probe();
    int start;
    LogEntry entry;
    char line[192];
    char value[16];
    const char* p = args;

    if (!p) p = "";
    while (*p == ' ') p++;

    if (total > 256) total = 256;

    if (*p) {
        if (!parse_nonneg_int(p, &requested) || requested <= 0) {
            print_string("Usage: log show [count]", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
    }

    if (total <= 0) {
        print_string("Logger buffer is empty", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    if (requested > total) requested = total;
    start = total - requested;

    line[0] = 0;
    str_concat(line, "Showing last ");
    int_to_str(requested, value); str_concat(line, value);
    str_concat(line, " of ");
    int_to_str(total, value); str_concat(line, value);
    str_concat(line, " log entries");
    print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

    for (int i = start; i < total; i++) {
        if (!log_get_entry(i, &entry)) continue;

        line[0] = 0;
        str_concat(line, "[");
        int_to_str((int)entry.tick, value); str_concat(line, value);
        str_concat(line, "] ");
        str_concat(line, log_level_name(entry.level));
        str_concat(line, ": ");
        str_concat(line, entry.message);

        print_string(line, -1, video, cursor, log_level_color(entry.level));
    }
}

static void handle_log_command(const char* args, char* video, int* cursor) {
    const char* p = args;
    int current_level;

    ensure_logger_ready();

    current_level = log_get_level();
    if (current_level < LOG_LEVEL_DEBUG || current_level > LOG_LEVEL_ERROR) {
        log_set_level(LOG_LEVEL_INFO);
        current_level = LOG_LEVEL_INFO;
    }

    if (!p) p = "";
    while (*p == ' ') p++;

    if (*p == 0) {
        char line[96];
        char value[16];

        line[0] = 0;
        str_concat(line, "Logger level: ");
        str_concat(line, log_level_name(current_level));
        str_concat(line, " (0=debug..3=error)");
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        line[0] = 0;
        str_concat(line, "Buffered entries: ");
        {
            int total = log_count_safe_probe();
            int_to_str(total, value);
        }
        str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

        print_string("Usage: log show [count] | log level [name|0-3] | log clear", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    if (p[0] == 's' && p[1] == 'h' && p[2] == 'o' && p[3] == 'w' && (p[4] == 0 || p[4] == ' ')) {
        handle_log_show_command(p[4] == 0 ? "" : p + 5, video, cursor);
        return;
    }

    if (p[0] == 'c' && p[1] == 'l' && p[2] == 'e' && p[3] == 'a' && p[4] == 'r' && p[5] == 0) {
        log_clear();
        print_string("Logger buffer cleared", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    if (p[0] == 't' && p[1] == 'e' && p[2] == 's' && p[3] == 't' && p[4] == 0) {
        log_write(LOG_LEVEL_INFO, "Logger test entry");
        print_string("Logger test entry written", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    if (p[0] == 'l' && p[1] == 'e' && p[2] == 'v' && p[3] == 'e' && p[4] == 'l' && (p[5] == 0 || p[5] == ' ')) {
        if (p[5] == 0) {
            char line[64];
            line[0] = 0;
            str_concat(line, "Logger level: ");
            str_concat(line, log_level_name(current_level));
            print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
            return;
        }

        while (*p == ' ') p++;
        p += 6;
        while (*p == ' ') p++;

        {
            int level = LOG_LEVEL_INFO;
            if (!parse_log_level(p, &level)) {
                print_string("Invalid log level. Use debug|info|warn|error or 0-3", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }

            log_set_level(level);
            {
                char line[80];
                line[0] = 0;
                str_concat(line, "Logger level set to ");
                str_concat(line, log_level_name(level));
                print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
            }
            return;
        }
    }

    print_string("Usage: log show [count] | log level [name|0-3] | log clear | log test", -1, video, cursor, COLOR_YELLOW);
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

#define PKG_DB_PATH "/.pkgdb"
#define PKG_REPO_PATH "/.pkgrepo"
#define PKG_MAX_RECV 512
#define PKG_CHUNK_SIZE 384

static const char* skip_spaces(const char* s) {
    while (s && *s == ' ') s++;
    return s;
}

static int read_token(const char** input, char* out, int out_max) {
    const char* s = skip_spaces(*input);
    int i = 0;

    if (!s || !out || out_max <= 1 || *s == 0) return 0;

    while (*s && *s != ' ' && i < out_max - 1) {
        out[i++] = *s++;
    }
    out[i] = 0;
    *input = skip_spaces(s);
    return i > 0;
}

static void append_bounded(char* dst, int dst_max, const char* src) {
    int d = 0;
    int s = 0;

    if (!dst || !src || dst_max <= 1) return;

    while (dst[d] && d < dst_max - 1) d++;
    while (src[s] && d < dst_max - 1) {
        dst[d++] = src[s++];
    }
    dst[d] = 0;
}

static void append_char_bounded(char* dst, int dst_max, char c) {
    int d = 0;
    if (!dst || dst_max <= 1) return;
    while (dst[d] && d < dst_max - 1) d++;
    if (d < dst_max - 1) {
        dst[d++] = c;
        dst[d] = 0;
    }
}

static void pkg_db_read(char* out, int out_max) {
    if (!out || out_max <= 1) return;
    out[0] = 0;
    shell_read_file_content(PKG_DB_PATH, out, out_max);
}

static int pkg_db_write(const char* content) {
    int len = content ? str_len(content) : 0;
    return shell_write_file_content(PKG_DB_PATH, content ? content : "", len, 0) >= 0;
}

static void pkg_repo_read_raw(char* out, int out_max) {
    if (!out || out_max <= 1) return;
    out[0] = 0;
    shell_read_file_content(PKG_REPO_PATH, out, out_max);
}

static int pkg_repo_write(const char* ip_text, int port) {
    char line[64];
    char port_text[16];
    line[0] = 0;
    append_bounded(line, sizeof(line), ip_text);
    append_char_bounded(line, sizeof(line), ' ');
    int_to_str(port, port_text);
    append_bounded(line, sizeof(line), port_text);
    return shell_write_file_content(PKG_REPO_PATH, line, str_len(line), 0) >= 0;
}

static int pkg_repo_get(char* out_ip_text, int out_ip_max, int* out_port) {
    char raw[64];
    const char* p;
    char ip_text[20];
    char port_text[12];
    int port = 0;

    if (!out_ip_text || !out_port || out_ip_max <= 1) return 0;

    pkg_repo_read_raw(raw, sizeof(raw));
    if (raw[0]) {
        p = raw;
        if (read_token(&p, ip_text, sizeof(ip_text)) &&
            read_token(&p, port_text, sizeof(port_text)) &&
            parse_ipv4_text(ip_text, (uint8_t[4]){0, 0, 0, 0}) &&
            parse_nonneg_int(port_text, &port) &&
            port >= 1 && port <= 65535) {
            str_copy(out_ip_text, ip_text, out_ip_max);
            *out_port = port;
            return 1;
        }
    }

    str_copy(out_ip_text, "10.0.2.2", out_ip_max);
    *out_port = 5555;
    return 1;
}

static int pkg_db_get_path(const char* package_name, char* out_path, int out_max) {
    char db[MAX_FILE_CONTENT];
    int i = 0;

    if (!package_name || !out_path || out_max <= 1) return 0;
    out_path[0] = 0;
    pkg_db_read(db, sizeof(db));

    while (db[i]) {
        char name[MAX_NAME_LENGTH];
        char path[MAX_PATH_LENGTH];
        int ni = 0;
        int pi = 0;

        while (db[i] == ' ') i++;
        if (db[i] == 0) break;
        while (db[i] && db[i] != ' ' && db[i] != '\n' && ni < MAX_NAME_LENGTH - 1) {
            name[ni++] = db[i++];
        }
        name[ni] = 0;

        while (db[i] == ' ') i++;
        while (db[i] && db[i] != '\n' && pi < MAX_PATH_LENGTH - 1) {
            path[pi++] = db[i++];
        }
        path[pi] = 0;

        while (db[i] == '\n') i++;

        if (name[0] && path[0] && mini_strcmp(name, package_name) == 0) {
            str_copy(out_path, path, out_max);
            return 1;
        }
    }

    return 0;
}

static int pkg_db_set_entry(const char* package_name, const char* install_path) {
    char old_db[MAX_FILE_CONTENT];
    char new_db[MAX_FILE_CONTENT];
    int i = 0;
    int replaced = 0;

    if (!package_name || !install_path || !package_name[0] || !install_path[0]) return 0;

    pkg_db_read(old_db, sizeof(old_db));
    new_db[0] = 0;

    while (old_db[i]) {
        char name[MAX_NAME_LENGTH];
        char path[MAX_PATH_LENGTH];
        int ni = 0;
        int pi = 0;

        while (old_db[i] == ' ') i++;
        if (old_db[i] == 0) break;

        while (old_db[i] && old_db[i] != ' ' && old_db[i] != '\n' && ni < MAX_NAME_LENGTH - 1) {
            name[ni++] = old_db[i++];
        }
        name[ni] = 0;

        while (old_db[i] == ' ') i++;
        while (old_db[i] && old_db[i] != '\n' && pi < MAX_PATH_LENGTH - 1) {
            path[pi++] = old_db[i++];
        }
        path[pi] = 0;

        while (old_db[i] == '\n') i++;

        if (!name[0] || !path[0]) continue;

        if (mini_strcmp(name, package_name) == 0) {
            if (!replaced) {
                append_bounded(new_db, sizeof(new_db), package_name);
                append_char_bounded(new_db, sizeof(new_db), ' ');
                append_bounded(new_db, sizeof(new_db), install_path);
                append_char_bounded(new_db, sizeof(new_db), '\n');
                replaced = 1;
            }
        } else {
            append_bounded(new_db, sizeof(new_db), name);
            append_char_bounded(new_db, sizeof(new_db), ' ');
            append_bounded(new_db, sizeof(new_db), path);
            append_char_bounded(new_db, sizeof(new_db), '\n');
        }
    }

    if (!replaced) {
        append_bounded(new_db, sizeof(new_db), package_name);
        append_char_bounded(new_db, sizeof(new_db), ' ');
        append_bounded(new_db, sizeof(new_db), install_path);
        append_char_bounded(new_db, sizeof(new_db), '\n');
    }

    return pkg_db_write(new_db);
}

static int pkg_db_remove_entry(const char* package_name) {
    char old_db[MAX_FILE_CONTENT];
    char new_db[MAX_FILE_CONTENT];
    int i = 0;
    int removed = 0;

    if (!package_name || !package_name[0]) return 0;

    pkg_db_read(old_db, sizeof(old_db));
    new_db[0] = 0;

    while (old_db[i]) {
        char name[MAX_NAME_LENGTH];
        char path[MAX_PATH_LENGTH];
        int ni = 0;
        int pi = 0;

        while (old_db[i] == ' ') i++;
        if (old_db[i] == 0) break;

        while (old_db[i] && old_db[i] != ' ' && old_db[i] != '\n' && ni < MAX_NAME_LENGTH - 1) {
            name[ni++] = old_db[i++];
        }
        name[ni] = 0;

        while (old_db[i] == ' ') i++;
        while (old_db[i] && old_db[i] != '\n' && pi < MAX_PATH_LENGTH - 1) {
            path[pi++] = old_db[i++];
        }
        path[pi] = 0;

        while (old_db[i] == '\n') i++;

        if (!name[0] || !path[0]) continue;

        if (mini_strcmp(name, package_name) == 0) {
            removed = 1;
            continue;
        }

        append_bounded(new_db, sizeof(new_db), name);
        append_char_bounded(new_db, sizeof(new_db), ' ');
        append_bounded(new_db, sizeof(new_db), path);
        append_char_bounded(new_db, sizeof(new_db), '\n');
    }

    if (!removed) return 0;
    return pkg_db_write(new_db);
}

static int pkg_exchange(const uint8_t server_ip[4], int server_port, const char* request, uint8_t* payload, int payload_max, int* out_payload_len) {
    int fd;
    int send_result;
    int recv_result = 0;
    int init_result;
    uint8_t src_ip[4];
    uint16_t src_port = 0;
    int payload_len = 0;
    uint8_t local_ip[4] = {10, 0, 2, 15};
    Rtl8139Status nic_status;

    if (!server_ip || !request || !payload || !out_payload_len || payload_max <= 0) return -1;
    *out_payload_len = 0;

    if (!arp_get_local_ip(src_ip)) {
        arp_set_local_ip(local_ip);
    }

    if (!rtl8139_get_status(&nic_status) || !nic_status.initialized) {
        init_result = rtl8139_init();
        if (init_result <= 0) {
            return -6;
        }
    }

    fd = sock_open_udp();
    if (fd < 0) return -2;

    send_result = sock_sendto(fd, server_ip, (uint16_t)server_port, (const uint8_t*)request, str_len(request));
    if (send_result == -4) {
        for (int attempt = 0; attempt < 6 && send_result == -4; attempt++) {
            arp_send_request(server_ip);
            for (int i = 0; i < 8192; i++) {
                net_poll_once();
                for (volatile int d = 0; d < 500; d++) ;
            }
            send_result = sock_sendto(fd, server_ip, (uint16_t)server_port, (const uint8_t*)request, str_len(request));
        }
    }

    if (send_result <= 0) {
        sock_close(fd);
        return send_result == -4 ? -4 : -3;
    }

// One shot: sock_recvfrom now spins 8192 NIC polls internally, which is
    // enough to cover QEMU's virtual network round-trip without retransmitting.
    recv_result = sock_recvfrom(fd, src_ip, &src_port, payload, payload_max, &payload_len);
    if (recv_result <= 0) {
        // One retry: resend and spin again.
        int rs = sock_sendto(fd, server_ip, (uint16_t)server_port, (const uint8_t*)request, str_len(request));
        if (rs == -4) {
            arp_send_request(server_ip);
            for (int i = 0; i < 8192; i++) {
                net_poll_once();
                for (volatile int d = 0; d < 500; d++) ;
            }
            (void)sock_sendto(fd, server_ip, (uint16_t)server_port, (const uint8_t*)request, str_len(request));
        }
        recv_result = sock_recvfrom(fd, src_ip, &src_port, payload, payload_max, &payload_len);
    }

    sock_close(fd);

    if (recv_result <= 0) return -5;
    if (payload_len < 0) payload_len = 0;
    if (payload_len > payload_max) payload_len = payload_max;
    *out_payload_len = payload_len;
    return 1;
}

static int parse_nonneg_int_prefix(const char* s, int* out, int* consumed) {
    int i = 0;
    int value = 0;

    if (!s || !out || !consumed) return 0;
    if (s[0] < '0' || s[0] > '9') return 0;

    while (s[i] >= '0' && s[i] <= '9') {
        value = value * 10 + (s[i] - '0');
        i++;
    }

    *out = value;
    *consumed = i;
    return 1;
}

static int pkg_parse_chunk_reply(const uint8_t* payload, int payload_len, int* out_data_offset, int* out_chunk_len) {
    int number = 0;
    int consumed = 0;
    int idx;

    if (!payload || !out_data_offset || !out_chunk_len) return 0;
    if (payload_len < 6) return 0;
    if (!(payload[0] == 'O' && payload[1] == 'K' && payload[2] == ' ')) return 0;

    if (!parse_nonneg_int_prefix((const char*)(payload + 3), &number, &consumed)) return 0;
    idx = 3 + consumed;
    if (idx >= payload_len || payload[idx] != '\n') return 0;
    idx++;

    if (number < 0) return 0;
    if (idx + number > payload_len) return 0;

    *out_data_offset = idx;
    *out_chunk_len = number;
    return 1;
}

static void pkg_print_repo(char* video, int* cursor) {
    char ip_text[20];
    int port = 0;
    char line[64];
    char port_text[16];

    if (!pkg_repo_get(ip_text, sizeof(ip_text), &port)) {
        print_string("PKG repo: unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    line[0] = 0;
    str_concat(line, "PKG repo: ");
    str_concat(line, ip_text);
    str_concat(line, ":");
    int_to_str(port, port_text);
    str_concat(line, port_text);
    print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
}

static void handle_pkg_repo_command(const char* args, char* video, int* cursor) {
    const char* p = args;
    char a[20];
    char b[12];
    int port = 0;
    uint8_t ip[4];

    if (!read_token(&p, a, sizeof(a))) {
        pkg_print_repo(video, cursor);
        return;
    }

    if (mini_strcmp(a, "show") == 0) {
        pkg_print_repo(video, cursor);
        return;
    }

    if (mini_strcmp(a, "set") == 0) {
        if (!read_token(&p, a, sizeof(a)) || !read_token(&p, b, sizeof(b))) {
            print_string("Usage: pkg repo <ip> <port>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
    } else {
        if (!read_token(&p, b, sizeof(b))) {
            print_string("Usage: pkg repo <ip> <port>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
    }

    if (!parse_ipv4_text(a, ip) || !parse_nonneg_int(b, &port) || port < 1 || port > 65535) {
        print_string("PKG repo: invalid IP/port", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (!pkg_repo_write(a, port)) {
        print_string("PKG repo: save failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    print_string("PKG repo saved", -1, video, cursor, COLOR_LIGHT_GREEN);
    pkg_print_repo(video, cursor);
}

static void handle_pkg_install_command(const char* args, char* video, int* cursor) {
    const char* p = args;
    char ip_text[20];
    char port_text[12];
    char tok1[32];
    char tok2[32];
    char tok3[32];
    char package_name[MAX_NAME_LENGTH];
    char request_package_name[MAX_NAME_LENGTH];
    char install_path[MAX_PATH_LENGTH];
    uint8_t server_ip[4];
    int server_port = 0;
    uint8_t payload[PKG_MAX_RECV + 1];
    int payload_len = 0;
    char file_content[MAX_FILE_CONTENT];
    int installed_len = 0;
    int xchg;
    int slash_pos = -1;
    int tried_name_fallback = 0;

    if (!read_token(&p, tok1, sizeof(tok1))) {
        print_string("Usage: pkg install <name> [path]", -1, video, cursor, COLOR_LIGHT_RED);
        print_string("Legacy: pkg install <ip> <port> <name> [path]", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    tok2[0] = 0;
    tok3[0] = 0;
    read_token(&p, tok2, sizeof(tok2));
    read_token(&p, tok3, sizeof(tok3));

    if (!tok2[0] && !tok3[0]) {
        for (int i = 0; tok1[i]; i++) {
            if (tok1[i] == '/') {
                slash_pos = i;
                break;
            }
        }
        if (slash_pos > 0) {
            int n = 0;
            while (n < slash_pos && n < (int)sizeof(package_name) - 1) {
                package_name[n] = tok1[n];
                n++;
            }
            package_name[n] = 0;
            str_copy(request_package_name, package_name, sizeof(request_package_name));
            str_copy(install_path, tok1 + slash_pos, sizeof(install_path));

            pkg_repo_get(ip_text, sizeof(ip_text), &server_port);
            if (!parse_ipv4_text(ip_text, server_ip) || server_port < 1 || server_port > 65535) {
                print_string("PKG: no valid repo configured", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }

            {
                char line[96];
                char val[16];
                line[0] = 0;
                str_concat(line, "PKG: using repo ");
                str_concat(line, ip_text);
                str_concat(line, ":");
                int_to_str(server_port, val);
                str_concat(line, val);
                print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
            }

            goto pkg_install_normalize_path;
        }
    }

    if (tok3[0] && parse_ipv4_text(tok1, server_ip) && parse_nonneg_int(tok2, &server_port) && server_port >= 1 && server_port <= 65535) {
        str_copy(ip_text, tok1, sizeof(ip_text));
        str_copy(port_text, tok2, sizeof(port_text));
        str_copy(package_name, tok3, sizeof(package_name));
        str_copy(request_package_name, package_name, sizeof(request_package_name));
        if (!read_token(&p, install_path, sizeof(install_path))) {
            str_copy(install_path, package_name, sizeof(install_path));
        }
    } else {
        str_copy(package_name, tok1, sizeof(package_name));
        str_copy(request_package_name, package_name, sizeof(request_package_name));
        if (tok2[0]) str_copy(install_path, tok2, sizeof(install_path));
        else str_copy(install_path, package_name, sizeof(install_path));

        pkg_repo_get(ip_text, sizeof(ip_text), &server_port);
        if (!parse_ipv4_text(ip_text, server_ip) || server_port < 1 || server_port > 65535) {
            print_string("PKG: no valid repo configured", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        {
            char line[96];
            char val[16];
            line[0] = 0;
            str_concat(line, "PKG: using repo ");
            str_concat(line, ip_text);
            str_concat(line, ":");
            int_to_str(server_port, val);
            str_concat(line, val);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
        }
    }

pkg_install_normalize_path:
    {
        char resolved_install_path[MAX_PATH_LENGTH];
        if (!shell_resolve_required_path(install_path, resolved_install_path, "PKG: invalid install path", video, cursor)) {
            return;
        }
        str_copy(install_path, resolved_install_path, sizeof(install_path));
    }

    for (int offset = 0; offset < MAX_FILE_CONTENT - 1;) {
        char request[128];
        char num[16];
        int data_offset = 0;
        int chunk_len = 0;

        request[0] = 0;
        append_bounded(request, sizeof(request), "GETCHUNK ");
        append_bounded(request, sizeof(request), request_package_name);
        append_char_bounded(request, sizeof(request), ' ');
        int_to_str(offset, num);
        append_bounded(request, sizeof(request), num);
        append_char_bounded(request, sizeof(request), ' ');
        int_to_str(PKG_CHUNK_SIZE, num);
        append_bounded(request, sizeof(request), num);

        xchg = pkg_exchange(server_ip, server_port, request, payload, PKG_MAX_RECV, &payload_len);
        if (xchg <= 0) {
            if (!tried_name_fallback && offset == 0) {
                int len = str_len(request_package_name);
                if (len > 4 &&
                    request_package_name[len - 4] == '.' &&
                    request_package_name[len - 3] == 'b' &&
                    request_package_name[len - 2] == 'a' &&
                    request_package_name[len - 1] == 's') {
                    request_package_name[len - 4] = 0;
                    tried_name_fallback = 1;
                    continue;
                } else if (len + 4 < MAX_NAME_LENGTH) {
                    str_concat(request_package_name, ".bas");
                    tried_name_fallback = 1;
                    continue;
                }
            }

            char code[16];
            int_to_str(xchg, code);
            if (xchg == -6) {
                print_string("PKG: network adapter init failed", -1, video, cursor, COLOR_LIGHT_RED);
            } else if (xchg == -4) {
                print_string("PKG: server MAC unknown (run arp whohas first)", -1, video, cursor, COLOR_YELLOW);
            } else {
                char line[64];
                line[0] = 0;
                str_concat(line, "PKG: request failed code=");
                str_concat(line, code);
                print_string(line, -1, video, cursor, COLOR_LIGHT_RED);
            }
            return;
        }

        if (payload_len < 0) payload_len = 0;
        if (payload_len > PKG_MAX_RECV) payload_len = PKG_MAX_RECV;
        payload[payload_len] = 0;

        if (payload_len >= 4 && payload[0] == 'E' && payload[1] == 'R' && payload[2] == 'R' && payload[3] == ' ') {
            print_string((const char*)payload, payload_len, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (!pkg_parse_chunk_reply(payload, payload_len, &data_offset, &chunk_len)) {
            print_string("PKG: invalid chunk reply", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (chunk_len == 0) break;

        if (installed_len + chunk_len > MAX_FILE_CONTENT - 1) {
            print_string("PKG: package exceeds FS file size limit", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        for (int i = 0; i < chunk_len; i++) {
            file_content[installed_len + i] = (char)payload[data_offset + i];
        }
        installed_len += chunk_len;
        offset += chunk_len;

        if (chunk_len < PKG_CHUNK_SIZE) break;
    }

    if (installed_len <= 0) {
        print_string("PKG: empty package payload", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    file_content[installed_len] = 0;

    if (shell_write_file_content(install_path, file_content, installed_len, 0) < 0) {
        print_string("PKG: install write failed", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (!pkg_db_set_entry(package_name, install_path)) {
        print_string("PKG: installed, but package index update failed", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    {
        char line[128];
        line[0] = 0;
        str_concat(line, "PKG: installed ");
        str_concat(line, package_name);
        str_concat(line, " -> ");
        str_concat(line, install_path);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static void handle_pkg_list_command(char* video, int* cursor) {
    char db[MAX_FILE_CONTENT];
    int i = 0;
    int count = 0;

    pkg_db_read(db, sizeof(db));
    if (db[0] == 0) {
        print_string("PKG: no installed packages", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    while (db[i]) {
        char line[160];
        int li = 0;

        while (db[i] == '\n') i++;
        if (db[i] == 0) break;

        while (db[i] && db[i] != '\n' && li < (int)sizeof(line) - 1) {
            line[li++] = db[i++];
        }
        line[li] = 0;
        while (db[i] == '\n') i++;

        if (line[0]) {
            print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
            count++;
        }
    }

    if (count == 0) {
        print_string("PKG: no installed packages", -1, video, cursor, COLOR_YELLOW);
    }
}

static void handle_pkg_search_command(char* video, int* cursor) {
    char ip_text[20];
    int port = 0;
    uint8_t server_ip[4];
    uint8_t payload[PKG_MAX_RECV + 1];
    int payload_len = 0;
    int xchg;
    int i = 0;
    int printed = 0;

    if (!pkg_repo_get(ip_text, sizeof(ip_text), &port) || !parse_ipv4_text(ip_text, server_ip) || port < 1 || port > 65535) {
        print_string("PKG: no valid repo configured", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    xchg = pkg_exchange(server_ip, port, "LIST", payload, PKG_MAX_RECV, &payload_len);
    if (xchg <= 0) {
        if (xchg == -6) {
            print_string("PKG: network adapter init failed", -1, video, cursor, COLOR_LIGHT_RED);
        } else {
            char line[64];
            char code[16];
            int_to_str(xchg, code);
            line[0] = 0;
            str_concat(line, "PKG: repository query failed code=");
            str_concat(line, code);
            print_string(line, -1, video, cursor, COLOR_LIGHT_RED);
        }
        return;
    }

    payload[payload_len] = 0;
    if (payload_len >= 4 && payload[0] == 'E' && payload[1] == 'R' && payload[2] == 'R' && payload[3] == ' ') {
        print_string((const char*)payload, payload_len, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (payload_len >= 3 && payload[0] == 'O' && payload[1] == 'K' && (payload[2] == '\n' || payload[2] == ' ')) {
        i = 3;
    }

    while (i < payload_len) {
        char line[96];
        int li = 0;
        while (i < payload_len && (payload[i] == '\n' || payload[i] == '\r')) i++;
        while (i < payload_len && payload[i] != '\n' && payload[i] != '\r' && li < (int)sizeof(line) - 1) {
            line[li++] = (char)payload[i++];
        }
        line[li] = 0;
        while (i < payload_len && payload[i] != '\n' && payload[i] != '\r') i++;
        if (line[0]) {
            print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
            printed++;
        }
    }

    if (!printed) {
        print_string("PKG: no packages listed", -1, video, cursor, COLOR_YELLOW);
    }
}

static void handle_pkg_remove_command(const char* args, char* video, int* cursor) {
    const char* p = args;
    char package_name[MAX_NAME_LENGTH];
    char install_path[MAX_PATH_LENGTH];
    int rm_result;

    if (!read_token(&p, package_name, sizeof(package_name))) {
        print_string("Usage: pkg remove <name>", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (!pkg_db_get_path(package_name, install_path, sizeof(install_path))) {
        print_string("PKG: package not found in index", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    {
        char resolved_install_path[MAX_PATH_LENGTH];
        if (!shell_resolve_required_path(install_path, resolved_install_path, "PKG: invalid install path", video, cursor)) {
            return;
        }
        rm_result = vfs_unlink(resolved_install_path);
    }
    if (rm_result < 0) {
        print_string("PKG: failed to remove installed file", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    if (!pkg_db_remove_entry(package_name)) {
        print_string("PKG: removed file, but index cleanup failed", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    {
        char line[128];
        line[0] = 0;
        str_concat(line, "PKG: removed ");
        str_concat(line, package_name);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
    }
}

static int udpecho_step_once(char* video, int* cursor) {
    uint8_t src_ip[4];
    uint16_t src_port = 0;
    uint8_t payload[513];
    int payload_len = 0;
    int r;

    if (udpecho_fd < 0) return -1;

    r = sock_recvfrom(udpecho_fd, src_ip, &src_port, payload, 512, &payload_len);
    if (r <= 0) return r;

    if (payload_len < 0) payload_len = 0;
    if (payload_len > 512) payload_len = 512;

    if (sock_sendto(udpecho_fd, src_ip, src_port, payload, payload_len) <= 0) {
        print_string("UDPECHO: reply send failed", -1, video, cursor, COLOR_LIGHT_RED);
        return -2;
    }

    {
        char line[96];
        char value[24];
        line[0] = 0;
        str_concat(line, "UDPECHO: echoed ");
        int_to_str(payload_len, value); str_concat(line, value);
        str_concat(line, " bytes to ");
        int_to_str(src_ip[0], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[1], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[2], value); str_concat(line, value); str_concat(line, ".");
        int_to_str(src_ip[3], value); str_concat(line, value);
        str_concat(line, ":");
        int_to_str((int)src_port, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
    }

    return 1;
}

static void handle_spawn_command(const char* arg, char* video, int* cursor) {
    while (*arg == ' ') arg++;

    if (arg[0] == 'r' && arg[1] == 'i' && arg[2] == 'n' && arg[3] == 'g' && arg[4] == '3' &&
        arg[5] == 'p' && arg[6] == 'f' && arg[7] == 0) {
        int pid = process_spawn_ring3_fault_demo();
        if (pid < 0) {
            print_string("Failed to spawn ring3 fault-test process", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        print_string("Spawned ring3 fault-test process", -1, video, cursor, COLOR_YELLOW);
        schedule();
        return;
    }

    if (arg[0] == 'r' && arg[1] == 'i' && arg[2] == 'n' && arg[3] == 'g' && arg[4] == '3' && arg[5] == 0) {
        int pid = process_spawn_ring3_demo();
        if (pid < 0) {
            print_string("Failed to spawn ring3 process", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        print_string("Spawned ring3 user-mode process", -1, video, cursor, COLOR_LIGHT_GREEN);
        schedule();
        return;
    }

    if (!(arg[0] == 'd' && arg[1] == 'e' && arg[2] == 'm' && arg[3] == 'o' && (arg[4] == 0 || arg[4] == ' '))) {
        print_string("Usage: spawn demo [count|auto on|auto off] | spawn ring3 | spawn ring3pf", -1, video, cursor, COLOR_LIGHT_RED);
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
    print_string("PID NAME   STATE    RUN   WORK", -1, video, cursor, COLOR_LIGHT_GRAY);

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
    char summary[64];
    char tmp[16];
    int total = 0;
    int passed = 0;
    unsigned int before = syscall_invoke(SYS_GET_TICKS);
    unsigned int pid = syscall_invoke(SYS_GET_PID);
    unsigned int yield_ret = syscall_invoke(SYS_YIELD);
    unsigned int after_yield = syscall_invoke(SYS_GET_TICKS);
    unsigned int waited = syscall_invoke1(SYS_WAIT_TICKS, 2u);
    unsigned int waited_zero = syscall_invoke1(SYS_WAIT_TICKS, 0u);

    print_string("syscalltest: running checks...", -1, video, cursor, COLOR_LIGHT_GRAY);

    total++;
    if (after_yield >= before) {
        passed++;
        print_string("syscalltest: get_ticks monotonic PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("syscalltest: get_ticks monotonic FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (pid == 0xFFFFFFFFu || pid < (unsigned int)MAX_PROCESSES) {
        passed++;
        print_string("syscalltest: get_pid return shape PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("syscalltest: get_pid return shape FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (yield_ret == 0u) {
        passed++;
        print_string("syscalltest: yield return PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("syscalltest: yield return FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (waited >= before + 2u) {
        passed++;
        print_string("syscalltest: wait_ticks(2) delay PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("syscalltest: wait_ticks(2) delay FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (waited_zero >= waited) {
        passed++;
        print_string("syscalltest: wait_ticks(0) non-regress PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("syscalltest: wait_ticks(0) non-regress FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    summary[0] = 0;
    str_concat(summary, "syscalltest: summary ");
    int_to_str(passed, tmp);
    str_concat(summary, tmp);
    str_concat(summary, "/");
    int_to_str(total, tmp);
    str_concat(summary, tmp);
    str_concat(summary, " checks passed");

    if (passed == total) {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_nettest_command(char* video, int* cursor) {
    char summary[64];
    char tmp[16];
    int total = 0;
    int passed = 0;
    Rtl8139Status nic;
    UDPStats udp_stats;
    TCPStats tcp_stats;
    uint8_t local_ip[4];
    int sock_fd = -1;
    int poll_result = 0;
    uint16_t udp_saved_port = 0;
    uint16_t tcp_saved_port = 0;
    int udp_had_listen = 0;
    int tcp_had_listen = 0;

    print_string("nettest: running checks...", -1, video, cursor, COLOR_LIGHT_GRAY);

    total++;
    if (rtl8139_get_status(&nic) && nic.present && nic.initialized) {
        passed++;
        print_string("nettest: rtl8139 initialized PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("nettest: rtl8139 initialized FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (arp_get_local_ip(local_ip)) {
        passed++;
        print_string("nettest: local IP configured PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("nettest: local IP configured FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (arp_get_cache_count() >= 0) {
        passed++;
        print_string("nettest: ARP cache query PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("nettest: ARP cache query FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (udp_get_stats(&udp_stats)) {
        passed++;
        print_string("nettest: UDP stats query PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("nettest: UDP stats query FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (tcp_get_stats(&tcp_stats)) {
        passed++;
        print_string("nettest: TCP stats query PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("nettest: TCP stats query FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    sock_fd = sock_open_udp();
    if (sock_fd >= 0) {
        passed++;
        print_string("nettest: UDP socket open PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        sock_close(sock_fd);
        sock_fd = -1;
    } else {
        print_string("nettest: UDP socket open FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    poll_result = net_poll_once();
    if (poll_result >= -500) {
        passed++;
        print_string("nettest: net dispatcher poll PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("nettest: net dispatcher poll FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    udp_had_listen = udp_get_listen_port(&udp_saved_port);
    total++;
    {
        uint16_t check_port = 0;
        if (udp_set_listen_port(40001u) && udp_get_listen_port(&check_port) && check_port == 40001u) {
            passed++;
            print_string("nettest: UDP listen set/show PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("nettest: UDP listen set/show FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
    }
    if (udp_had_listen) udp_set_listen_port(udp_saved_port);
    else udp_clear_listen_port();

    tcp_had_listen = tcp_get_listen_port(&tcp_saved_port);
    total++;
    {
        uint16_t check_port = 0;
        if (tcp_set_listen_port(40002u) && tcp_get_listen_port(&check_port) && check_port == 40002u) {
            passed++;
            print_string("nettest: TCP listen set/show PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("nettest: TCP listen set/show FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
    }
    if (tcp_had_listen) tcp_set_listen_port(tcp_saved_port);
    else tcp_clear_listen_port();

    summary[0] = 0;
    str_concat(summary, "nettest: summary ");
    int_to_str(passed, tmp);
    str_concat(summary, tmp);
    str_concat(summary, "/");
    int_to_str(total, tmp);
    str_concat(summary, tmp);
    str_concat(summary, " checks passed");

    if (passed == total) {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_memtest_command(char* video, int* cursor) {
    char summary[64];
    char tmp[16];
    int total = 0;
    int passed = 0;
    int smoke_result = memory_smoke_test();
    unsigned char* user_code_page = 0;
    unsigned char* user_stack_page = 0;
    unsigned int test_pd = 0;
    unsigned int phys = 0;
    unsigned int flags = 0;

    print_string("memtest: running checks...", -1, video, cursor, COLOR_LIGHT_GRAY);

    total++;
    if (smoke_result == 1) {
        passed++;
        print_string("memtest: allocator zeroing/free-realloc PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: allocator zeroing/free-realloc FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    user_code_page = (unsigned char*)alloc_page();
    total++;
    if (user_code_page) {
        passed++;
        print_string("memtest: allocate user code page PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: allocate user code page FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    user_stack_page = (unsigned char*)alloc_page();
    total++;
    if (user_stack_page) {
        passed++;
        print_string("memtest: allocate user stack page PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: allocate user stack page FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (user_code_page && paging_get_page_ref((unsigned int)(uintptr_t)user_code_page) == 1u) {
        passed++;
        print_string("memtest: code page refcount initialized PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: code page refcount initialized FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (user_stack_page && paging_get_page_ref((unsigned int)(uintptr_t)user_stack_page) == 1u) {
        passed++;
        print_string("memtest: stack page refcount initialized PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: stack page refcount initialized FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    if (user_code_page && user_stack_page) {
        test_pd = paging_create_process_directory((unsigned int)(uintptr_t)user_code_page,
                                                  (unsigned int)(uintptr_t)user_stack_page,
                                                  4096u);
    }

    total++;
    if (test_pd != 0u) {
        passed++;
        print_string("memtest: create process page directory PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: create process page directory FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (test_pd != 0u && paging_get_mapping(test_pd,
                                            (unsigned int)(uintptr_t)user_stack_page,
                                            &phys,
                                            &flags) == 0) {
        passed++;
        print_string("memtest: stack page mapping present PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: stack page mapping present FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (test_pd != 0u && (flags & PAGE_FLAG_USER) != 0u) {
        passed++;
        print_string("memtest: stack page user flag PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: stack page user flag FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (test_pd != 0u && paging_mark_user_range(test_pd,
                                                (unsigned int)(uintptr_t)user_code_page,
                                                0u) != 0) {
        passed++;
        print_string("memtest: reject zero-length user range PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: reject zero-length user range FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (test_pd != 0u && paging_set_page_writable(test_pd,
                                                  (unsigned int)(uintptr_t)user_stack_page,
                                                  0) == 0) {
        passed++;
        print_string("memtest: clear page writable bit PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: clear page writable bit FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (test_pd != 0u && paging_get_mapping(test_pd,
                                            (unsigned int)(uintptr_t)user_stack_page,
                                            &phys,
                                            &flags) == 0 &&
        (flags & PAGE_FLAG_RW) == 0u) {
        passed++;
        print_string("memtest: writable bit cleared verification PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: writable bit cleared verification FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (test_pd != 0u && paging_set_page_writable(test_pd,
                                                  (unsigned int)(uintptr_t)user_stack_page,
                                                  1) == 0) {
        passed++;
        print_string("memtest: restore page writable bit PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("memtest: restore page writable bit FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    if (test_pd != 0u) {
        paging_destroy_process_directory(test_pd);
    }
    if (user_stack_page) {
        free_page(user_stack_page);
    }
    if (user_code_page) {
        free_page(user_code_page);
    }

    summary[0] = 0;
    str_concat(summary, "memtest: summary ");
    int_to_str(passed, tmp);
    str_concat(summary, tmp);
    str_concat(summary, "/");
    int_to_str(total, tmp);
    str_concat(summary, tmp);
    str_concat(summary, " checks passed");

    if (passed == total) {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_pathtest_command(char* video, int* cursor) {
    char normalized[128];
    char readback[64];
    const char* payload = "pathtest: io ok";
    const char* canonical_path = "/pathtest_file";
    char summary[64];
    char tmp[16];
    int fd = -1;
    int n = 0;
    int total = 0;
    int passed = 0;

    print_string("pathtest: running checks...", -1, video, cursor, COLOR_LIGHT_GRAY);

    total++;
    if (fs_path_normalize("/a//b/./c/../d", normalized, (int)sizeof(normalized)) &&
        mini_strcmp(normalized, "/a/b/d") == 0) {
        passed++;
        print_string("pathtest: normalize mixed separators/dots PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: normalize mixed separators/dots FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (fs_path_normalize("/../../x", normalized, (int)sizeof(normalized)) &&
        mini_strcmp(normalized, "/x") == 0) {
        passed++;
        print_string("pathtest: normalize parent-beyond-root PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: normalize parent-beyond-root FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (fs_path_normalize("/", normalized, (int)sizeof(normalized)) &&
        mini_strcmp(normalized, "/") == 0) {
        passed++;
        print_string("pathtest: normalize root PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: normalize root FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    {
        char too_many_segments[160];
        int pos = 0;
        for (int i = 0; i < 34 && pos < (int)sizeof(too_many_segments) - 2; i++) {
            too_many_segments[pos++] = '/';
            too_many_segments[pos++] = 'a';
        }
        too_many_segments[pos] = 0;

        if (!fs_path_normalize(too_many_segments, normalized, (int)sizeof(normalized))) {
            passed++;
            print_string("pathtest: normalize segment-limit rejection PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("pathtest: normalize segment-limit rejection FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
    }

    (void)vfs_unlink(canonical_path);

    total++;
    fd = vfs_open("/./tmp/../pathtest_file", FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    if (fd >= 0) {
        passed++;
        print_string("pathtest: open normalized path for write PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: open normalized path for write FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (fd >= 0) {
        n = vfs_write(fd, payload, str_len(payload));
        if (n == str_len(payload)) {
            passed++;
            print_string("pathtest: write payload PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("pathtest: write payload FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
        vfs_close(fd);
        fd = -1;
    } else {
        print_string("pathtest: write payload FAIL (open step failed)", -1, video, cursor, COLOR_LIGHT_RED);
    }

    for (int i = 0; i < (int)sizeof(readback); i++) readback[i] = 0;

    total++;
    fd = vfs_open(canonical_path, FS_O_READ);
    if (fd >= 0) {
        passed++;
        print_string("pathtest: reopen canonical path PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: reopen canonical path FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (fd >= 0) {
        n = vfs_read(fd, readback, (int)sizeof(readback) - 1);
        if (n == str_len(payload)) {
            passed++;
            print_string("pathtest: read payload length PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("pathtest: read payload length FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
        if (n >= 0 && n < (int)sizeof(readback)) readback[n] = 0;
        vfs_close(fd);
        fd = -1;
    } else {
        print_string("pathtest: read payload length FAIL (reopen step failed)", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (mini_strcmp(readback, payload) == 0) {
        passed++;
        print_string("pathtest: read payload content PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: read payload content FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (vfs_unlink(canonical_path) == 0) {
        passed++;
        print_string("pathtest: unlink test file PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("pathtest: unlink test file FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    {
        char long_path[305];
        int len = DIRENT_NAME_MAX + 1;
        long_path[0] = '/';
        for (int i = 0; i < len; i++) {
            long_path[i + 1] = 'a';
        }
        long_path[len + 1] = 0;

        fd = vfs_open(long_path, FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
        if (fd < 0) {
            passed++;
            print_string("pathtest: overlong name rejection PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            vfs_close(fd);
            (void)vfs_unlink(long_path);
            print_string("pathtest: overlong name rejection FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
    }

    summary[0] = 0;
    str_concat(summary, "pathtest: summary ");
    int_to_str(passed, tmp);
    str_concat(summary, tmp);
    str_concat(summary, "/");
    int_to_str(total, tmp);
    str_concat(summary, tmp);
    str_concat(summary, " checks passed");

    if (passed == total) {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_proctest_command(char* video, int* cursor) {
    char summary[64];
    char tmp[16];
    int total = 0;
    int passed = 0;
    int pid = -1;
    unsigned int before_ticks = 0;
    unsigned int after_ticks = 0;
    unsigned int before_run_ticks = 0;
    unsigned int after_run_ticks = 0;
    int kill_result = -1;

    print_string("proctest: running checks...", -1, video, cursor, COLOR_LIGHT_GRAY);

    total++;
    pid = process_spawn_demo_with_work(60u);
    if (pid >= 0) {
        passed++;
        print_string("proctest: spawn demo process PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("proctest: spawn demo process FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (pid >= 0 && pid < MAX_PROCESSES &&
        process_table[pid].state != PROC_UNUSED &&
        process_table[pid].state != PROC_EXITED) {
        passed++;
        print_string("proctest: process visible in table PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("proctest: process visible in table FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (pid >= 0) {
        before_run_ticks = process_table[pid].run_ticks;
        before_ticks = syscall_invoke(1);
        schedule();
        (void)syscall_invoke1(3, 4u);
        after_ticks = syscall_invoke(1);
        after_run_ticks = process_table[pid].run_ticks;

        if (after_ticks >= before_ticks &&
            (after_run_ticks > before_run_ticks || process_table[pid].state == PROC_EXITED)) {
            passed++;
            print_string("proctest: scheduler/run tick progress PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("proctest: scheduler/run tick progress FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else {
        print_string("proctest: scheduler/run tick progress FAIL (spawn step failed)", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (pid >= 0) {
        kill_result = process_kill(pid);
        if (kill_result == 0) {
            passed++;
            print_string("proctest: kill process PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
        } else {
            print_string("proctest: kill process FAIL", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else {
        print_string("proctest: kill process FAIL (spawn step failed)", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (pid >= 0 && pid < MAX_PROCESSES && process_table[pid].state == PROC_EXITED) {
        passed++;
        print_string("proctest: process exit state recorded PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("proctest: process exit state recorded FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    total++;
    if (process_kill(-1) == -1) {
        passed++;
        print_string("proctest: invalid pid rejection PASS", -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string("proctest: invalid pid rejection FAIL", -1, video, cursor, COLOR_LIGHT_RED);
    }

    summary[0] = 0;
    str_concat(summary, "proctest: summary ");
    int_to_str(passed, tmp);
    str_concat(summary, tmp);
    str_concat(summary, "/");
    int_to_str(total, tmp);
    str_concat(summary, tmp);
    str_concat(summary, " checks passed");

    if (passed == total) {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else {
        print_string(summary, -1, video, cursor, COLOR_LIGHT_RED);
    }
}

static void handle_fdtest_command(const char* arg, char* video, int* cursor) {
    char path[MAX_PATH_LENGTH];
    char resolved[MAX_PATH_LENGTH];
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

    if (!shell_resolve_required_path(path, resolved, "Usage: fdtest <path>", video, cursor)) {
        return;
    }

    const char* payload = "fdtest: syscall write/read OK";
    int payload_len = str_len(payload);
    char readback[64];
    for (int i = 0; i < (int)sizeof(readback); i++) readback[i] = 0;

    int fdw = (int)syscall_invoke2(SYS_OPEN, (unsigned int)resolved, (unsigned int)(FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC));
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

    int fdr = (int)syscall_invoke2(SYS_OPEN, (unsigned int)resolved, (unsigned int)FS_O_READ);
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
    char content[MAX_FILE_CONTENT];
    int content_size;

    content_size = shell_read_file_content(filename, content, (int)sizeof(content));
    if (content_size < 0) {
        print_string("File not found", 14, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    char buf[4];
    for (int i = 0; i < content_size; i++) {
        byte_to_hex((unsigned char)content[i], buf);
        print_string(buf, -1, video, cursor, COLOR_CYAN);
    }
}

static void handle_history_command(char* video, int* cursor) {
    for (int i = 0; i < history_count; i++) {
        print_string(history[i], -1, video, cursor, COLOR_LIGHT_GRAY);
    }
}

static void handle_pwd_command(char* video, int* cursor) {
    if (!fs_runtime_ensure_newfs()) {
        print_string("Filesystem unavailable", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    shell_ensure_newfs_cwd();
    print_string(newfs_cwd, -1, video, cursor, COLOR_CYAN);
}

static int shell_prepare_create_path(const char* raw_name, char* out_path, int out_max) {
    (void)out_max;
    return shell_resolve_newfs_path(raw_name, out_path);
}

static int shell_resolve_required_path(const char* raw_path, char* out_path, const char* usage_msg, char* video, int* cursor) {
    if (!shell_resolve_newfs_path(raw_path, out_path)) {
        if (usage_msg && usage_msg[0]) {
            print_string(usage_msg, -1, video, cursor, COLOR_LIGHT_RED);
        }
        return 0;
    }
    return 1;
}

static void handle_savedir_command(const char* args, char* video, int* cursor) {
    char path[MAX_PATH_LENGTH];
    int arg_start = 0;
    int arg_end;

    if (!args) args = "";
    while (args[arg_start] == ' ') arg_start++;

    if (args[arg_start] == 0) {
        shell_ensure_newfs_cwd();
        print_string("Save directory: ", -1, video, cursor, COLOR_LIGHT_CYAN);
        print_string_sameline(newfs_cwd, -1, video, cursor, COLOR_LIGHT_CYAN);
        return;
    }

    if (args[arg_start] == 'c' && args[arg_start + 1] == 'l' && args[arg_start + 2] == 'e' &&
        args[arg_start + 3] == 'a' && args[arg_start + 4] == 'r' && args[arg_start + 5] == 0) {
        shell_save_dir_idx = -1;
        print_string("Save directory cleared", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    arg_end = str_len(args);
    while (arg_end > arg_start && args[arg_end - 1] == ' ') arg_end--;

    int pi = 0;
    for (int i = arg_start; i < arg_end && pi < MAX_PATH_LENGTH - 1; i++) {
        path[pi++] = args[i];
    }
    path[pi] = 0;

    if (path[0] == 0) {
        print_string("Usage: savedir [<path>|clear]", -1, video, cursor, COLOR_YELLOW);
        return;
    }

    char resolved[MAX_PATH_LENGTH];
    FInode inode;
    if (!shell_resolve_newfs_path(path, resolved) || vfs_stat(resolved, &inode) < 0 || !(inode.mode & INODE_MODE_DIR)) {
        print_string("Directory not found", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    str_copy(newfs_cwd, resolved, MAX_PATH_LENGTH);
    print_string("Save directory set to: ", -1, video, cursor, COLOR_LIGHT_GREEN);
    print_string_sameline(newfs_cwd, -1, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_touch_command(const char* filename, char* video, int* cursor) {
    char target_path[MAX_PATH_LENGTH];
    if (!shell_prepare_create_path(filename, target_path, MAX_PATH_LENGTH)) {
        print_string("Usage: touch <filename>", 23, video, cursor, COLOR_LIGHT_RED);
        return;
    }

    int fd = (int)syscall_invoke2(SYS_OPEN, (unsigned int)target_path, (unsigned int)(FS_O_WRITE | FS_O_CREATE));
    if (fd < 0) {
        print_string("Cannot create file", 18, video, cursor, COLOR_LIGHT_RED);
        return;
    }
    syscall_invoke1(SYS_CLOSE, (unsigned int)fd);
    print_string("File created", 12, video, cursor, COLOR_LIGHT_GREEN);
}

static void handle_tree_command(char* video, int* cursor) {
    shell_ensure_newfs_cwd();
    print_string(newfs_cwd, -1, video, cursor, 0xE);
    print_string("\n", 1, video, cursor, 0xE);
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
    
    if (pattern[0] == 0) {
        print_string("Usage: grep pattern filename", 28, video, cursor, 0xC);
        return;
    }

    // Search either file content or piped stdin content.
    char file_content[MAX_FILE_CONTENT];
    char* content = 0;
    int content_size = 0;
    if (filename[0] == 0) {
        if (!shell_stdin_data) {
            print_string("Usage: grep pattern filename", 28, video, cursor, 0xC);
            return;
        }
        content = (char*)shell_stdin_data;
        content_size = shell_stdin_len;
    } else {
        content = file_content;
        content_size = shell_read_file_content(filename, content, MAX_FILE_CONTENT);
        if (content_size < 0) {
            print_string("File not found", 14, video, cursor, 0xC);
            return;
        }
    }

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
    
    char content[MAX_FILE_CONTENT];
    int content_size = shell_read_file_content(source, content, (int)sizeof(content));
    if (content_size < 0) {
        print_string("Source file not found", 21, video, cursor, 0xC);
        return;
    }
    
    if (shell_write_file_content(dest, content, content_size, 0) < 0) {
        print_string("Cannot create destination file", 30, video, cursor, 0xC);
    } else {
        print_string("File copied", 11, video, cursor, 0xA);
    }
}

static void dispatch_command_internal(const char* cmd, char* video, int* cursor);

// --- Main Command Dispatcher ---
void dispatch_command(const char* cmd, char* video, int* cursor) {
    char left_cmd[MAX_CMD_BUFFER];
    char right_cmd[MAX_CMD_BUFFER];
    char base_cmd[MAX_CMD_BUFFER];
    char out_path[MAX_PATH_LENGTH];
    char in_path[MAX_PATH_LENGTH];
    char captured[4096];
    char stdin_buffer[MAX_FILE_CONTENT + 1];
    int pipe_pos;
    int out_pos = -1;
    int in_pos = -1;
    int append_mode = 0;
    int has_out = 0;
    int has_in = 0;

    if (!cmd) return;

    pipe_pos = shell_find_unquoted(cmd, '|');
    if (pipe_pos >= 0) {
        const char* prev_stdin = shell_stdin_data;
        int prev_stdin_len = shell_stdin_len;
        int prev_stdin_active = shell_stdin_active;
        int i = 0;
        int right_index = pipe_pos + 1;
        int captured_len;

        while (i < pipe_pos && i < MAX_CMD_BUFFER - 1) {
            left_cmd[i] = cmd[i];
            i++;
        }
        left_cmd[i] = 0;
        shell_trim_copy(base_cmd, left_cmd, MAX_CMD_BUFFER);
        shell_trim_copy(right_cmd, cmd + right_index, MAX_CMD_BUFFER);

        if (base_cmd[0] == 0 || right_cmd[0] == 0) {
            print_string("Invalid pipe syntax", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        display_begin_capture(captured, (int)sizeof(captured), 1);
        dispatch_command(base_cmd, video, cursor);
        captured_len = display_end_capture();

        shell_stdin_data = captured;
        shell_stdin_len = captured_len;
        shell_stdin_active = 1;
        dispatch_command(right_cmd, video, cursor);
        shell_stdin_data = prev_stdin;
        shell_stdin_len = prev_stdin_len;
        shell_stdin_active = prev_stdin_active;
        return;
    }

    if (!shell_starts_with_echo(cmd)) {
        out_pos = shell_find_unquoted(cmd, '>');
        if (out_pos >= 0) {
            int op_len = 1;
            int i = 0;
            if (cmd[out_pos + 1] == '>') {
                append_mode = 1;
                op_len = 2;
            }

            while (i < out_pos && i < MAX_CMD_BUFFER - 1) {
                base_cmd[i] = cmd[i];
                i++;
            }
            base_cmd[i] = 0;
            shell_trim_copy(base_cmd, base_cmd, MAX_CMD_BUFFER);
            shell_trim_copy(out_path, cmd + out_pos + op_len, MAX_PATH_LENGTH);

            if (base_cmd[0] == 0 || out_path[0] == 0) {
                print_string("Invalid redirection syntax", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }

            has_out = 1;
            cmd = base_cmd;
        }

        in_pos = shell_find_unquoted(cmd, '<');
        if (in_pos >= 0) {
            int i = 0;
            while (i < in_pos && i < MAX_CMD_BUFFER - 1) {
                base_cmd[i] = cmd[i];
                i++;
            }
            base_cmd[i] = 0;
            shell_trim_copy(base_cmd, base_cmd, MAX_CMD_BUFFER);
            shell_trim_copy(in_path, cmd + in_pos + 1, MAX_PATH_LENGTH);

            if (base_cmd[0] == 0 || in_path[0] == 0) {
                print_string("Invalid redirection syntax", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }

            has_in = 1;
            cmd = base_cmd;
        }
    }

    if (has_out || has_in) {
        const char* prev_stdin = shell_stdin_data;
        int prev_stdin_len = shell_stdin_len;
        int prev_stdin_active = shell_stdin_active;
        int captured_len = 0;

        if (has_in) {
            int read_len = shell_read_file_content(in_path, stdin_buffer, (int)sizeof(stdin_buffer));
            if (read_len < 0) {
                print_string("Input redirection file not found", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
            shell_stdin_data = stdin_buffer;
            shell_stdin_len = read_len;
            shell_stdin_active = 1;
        }

        if (has_out) {
            display_begin_capture(captured, (int)sizeof(captured), 1);
            dispatch_command_internal(cmd, video, cursor);
            captured_len = display_end_capture();
            if (shell_write_file_content(out_path, captured, captured_len, append_mode) < 0) {
                print_string("Output redirection failed", -1, video, cursor, COLOR_LIGHT_RED);
            }
        } else {
            dispatch_command_internal(cmd, video, cursor);
        }

        shell_stdin_data = prev_stdin;
        shell_stdin_len = prev_stdin_len;
        shell_stdin_active = prev_stdin_active;
        return;
    }

    dispatch_command_internal(cmd, video, cursor);
}

static void dispatch_command_internal(const char* cmd, char* video, int* cursor) {
                ensure_logger_ready();
                if (cmd && cmd[0]) {
                    char log_line[LOGGER_MESSAGE_MAX];
                    int out = 0;
                    const char* prefix = "cmd: ";

                    while (prefix[out] && out < LOGGER_MESSAGE_MAX - 1) {
                        log_line[out] = prefix[out];
                        out++;
                    }

                    for (int i = 0; cmd[i] && out < LOGGER_MESSAGE_MAX - 1; i++) {
                        log_line[out++] = cmd[i];
                    }
                    log_line[out] = 0;
                    log_write(LOG_LEVEL_INFO, log_line);
                }

                // creategroup <name>
                if (cmd[0] == 'c' && cmd[1] == 'r' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 't' && cmd[5] == 'e' && cmd[6] == 'g' && cmd[7] == 'r' && cmd[8] == 'o' && cmd[9] == 'u' && cmd[10] == 'p' && cmd[11] == ' ') {
                    extern int current_user_idx;
                    if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
                        print_string("Access denied: admin only.\n", -1, video, cursor, COLOR_LIGHT_RED);
                        return;
                    }
                    int i = 12;
                    while (cmd[i] == ' ') i++;
                    char name[MAX_NAME_LENGTH];
                    int n = 0;
                    while (cmd[i] && n < MAX_NAME_LENGTH-1) name[n++] = cmd[i++];
                    name[n] = 0;
                    if (name[0] == 0) {
                        print_string("Usage: creategroup <name>\n", -1, video, cursor, COLOR_YELLOW);
                        return;
                    }
                    // Check for duplicate
                    for (int g = 0; g < group_count; g++) {
                        if (group_table[g].used && mini_strcmp(name, group_table[g].name) == 0) {
                            print_string("Group already exists.\n", -1, video, cursor, COLOR_LIGHT_RED);
                            return;
                        }
                    }
                    if (group_count >= MAX_GROUPS) {
                        print_string("Max groups reached.\n", -1, video, cursor, COLOR_LIGHT_RED);
                        return;
                    }
                    // Find next available bitmask
                    unsigned int mask = 1;
                    for (int b = 0; b < 32; b++) {
                        int used = 0;
                        for (int g = 0; g < group_count; g++) {
                            if (group_table[g].used && group_table[g].bitmask == mask) used = 1;
                        }
                        if (!used) break;
                        mask <<= 1;
                    }
                    group_table[group_count].used = 1;
                    str_copy(group_table[group_count].name, name, MAX_NAME_LENGTH);
                    group_table[group_count].bitmask = mask;
                    group_count++;
                    print_string("Group created.\n", -1, video, cursor, COLOR_LIGHT_GREEN);
                    return;
                }

                // delgroup <name>
                if (cmd[0] == 'd' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'g' && cmd[4] == 'r' && cmd[5] == 'o' && cmd[6] == 'u' && cmd[7] == 'p' && cmd[8] == ' ') {
                    extern int current_user_idx;
                    if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
                        print_string("Access denied: admin only.\n", -1, video, cursor, COLOR_LIGHT_RED);
                        return;
                    }
                    int i = 9;
                    while (cmd[i] == ' ') i++;
                    char name[MAX_NAME_LENGTH];
                    int n = 0;
                    while (cmd[i] && n < MAX_NAME_LENGTH-1) name[n++] = cmd[i++];
                    name[n] = 0;
                    if (name[0] == 0) {
                        print_string("Usage: delgroup <name>\n", -1, video, cursor, COLOR_YELLOW);
                        return;
                    }
                    int found = 0;
                    for (int g = 0; g < group_count; g++) {
                        if (group_table[g].used && mini_strcmp(name, group_table[g].name) == 0) {
                            group_table[g].used = 0;
                            found = 1;
                            // Remove group from all users
                            for (int u = 0; u < user_count; u++) {
                                user_table[u].groups &= ~group_table[g].bitmask;
                            }
                            print_string("Group deleted.\n", -1, video, cursor, COLOR_LIGHT_GREEN);
                            break;
                        }
                    }
                    if (!found) print_string("Group not found.\n", -1, video, cursor, COLOR_LIGHT_RED);
                    return;
                }
            // listgroups: show all group names and bitmasks (dynamic)
            if (mini_strcmp(cmd, "listgroups") == 0) {
                print_string("Groups:\n", -1, video, cursor, COLOR_YELLOW);
                for (int g = 0; g < group_count; g++) {
                    if (group_table[g].used) {
                        char buf[64];
                        int n = 0;
                        buf[n++] = ' ';
                        buf[n++] = '0'; buf[n++] = 'x';
                        const char hex[] = "0123456789ABCDEF";
                        unsigned int m = group_table[g].bitmask;
                        buf[n++] = hex[(m >> 4) & 0xF];
                        buf[n++] = hex[m & 0xF];
                        buf[n++] = ' ';
                        int i = 0;
                        while (group_table[g].name[i] && n < 63) buf[n++] = group_table[g].name[i++];
                        buf[n++] = '\n';
                        buf[n] = 0;
                        print_string(buf, -1, video, cursor, COLOR_YELLOW);
                    }
                }
                return;
            }

            // lsgroup <mask|name>: list users in a group (by mask or name)
            if (cmd[0] == 'l' && cmd[1] == 's' && cmd[2] == 'g' && cmd[3] == 'r' && cmd[4] == 'o' && cmd[5] == 'u' && cmd[6] == 'p' && cmd[7] == ' ') {
                int i = 8;
                while (cmd[i] == ' ') i++;
                // Try to parse as mask (hex or decimal)
                int groupmask = -1;
                if (cmd[i] == '0' && (cmd[i+1] == 'x' || cmd[i+1] == 'X')) {
                    i += 2;
                    groupmask = 0;
                    while (cmd[i]) {
                        char c = cmd[i];
                        if (c >= '0' && c <= '9') groupmask = (groupmask << 4) | (c - '0');
                        else if (c >= 'a' && c <= 'f') groupmask = (groupmask << 4) | (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') groupmask = (groupmask << 4) | (c - 'A' + 10);
                        else break;
                        i++;
                    }
                } else if (cmd[i] >= '0' && cmd[i] <= '9') {
                    groupmask = 0;
                    while (cmd[i] >= '0' && cmd[i] <= '9') {
                        groupmask = groupmask * 10 + (cmd[i] - '0');
                        i++;
                    }
                } else {
                    // Try to match by group name
                    char name[MAX_NAME_LENGTH];
                    int n = 0;
                    while (cmd[i] && cmd[i] != ' ' && n < MAX_NAME_LENGTH-1) name[n++] = cmd[i++];
                    name[n] = 0;
                    for (int g = 0; g < group_count; g++) {
                        if (group_table[g].used && mini_strcmp(name, group_table[g].name) == 0) {
                            groupmask = group_table[g].bitmask;
                            break;
                        }
                    }
                }
                if (groupmask == -1) {
                    print_string("Group not found.\n", -1, video, cursor, COLOR_LIGHT_RED);
                    return;
                }
                int found = 0;
                for (int j = 0; j < user_count; j++) {
                    if ((user_table[j].groups & groupmask) != 0) {
                        print_string(user_table[j].username, -1, video, cursor, COLOR_LIGHT_GREEN);
                        print_string("\n", 1, video, cursor, COLOR_LIGHT_GREEN);
                        found = 1;
                    }
                }
                if (!found) print_string("No users in group.\n", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }

            // whois <username>: show user's group bitmask and group names (dynamic)
            if (cmd[0] == 'w' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'i' && cmd[4] == 's' && cmd[5] == ' ') {
                int i = 6;
                while (cmd[i] == ' ') i++;
                char username[MAX_NAME_LENGTH];
                int u = 0;
                while (cmd[i] && u < MAX_NAME_LENGTH-1) username[u++] = cmd[i++];
                username[u] = 0;
                int found = 0;
                for (int j = 0; j < user_count; j++) {
                    if (mini_strcmp(username, user_table[j].username) == 0) {
                        found = 1;
                        unsigned int g = user_table[j].groups;
                        char buf[32];
                        buf[0] = 'G'; buf[1] = 'r'; buf[2] = 'o'; buf[3] = 'u'; buf[4] = 'p'; buf[5] = 's'; buf[6] = ':'; buf[7] = ' '; buf[8] = '0'; buf[9] = 'x';
                        const char hex[] = "0123456789ABCDEF";
                        buf[10] = hex[(g >> 4) & 0xF];
                        buf[11] = hex[g & 0xF];
                        buf[12] = '\n';
                        buf[13] = 0;
                        print_string(buf, -1, video, cursor, COLOR_YELLOW);
                        // Print all group names for which user is a member
                        for (int gr = 0; gr < group_count; gr++) {
                            if (group_table[gr].used && (g & group_table[gr].bitmask)) {
                                print_string("  ", 2, video, cursor, COLOR_YELLOW);
                                print_string(group_table[gr].name, -1, video, cursor, COLOR_YELLOW);
                                print_string("\n", 1, video, cursor, COLOR_YELLOW);
                            }
                        }
                        break;
                    }
                }
                if (!found) print_string("User not found.\n", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
        // setgroups <username> <groupmask>
        if (cmd[0] == 's' && cmd[1] == 'e' && cmd[2] == 't' && cmd[3] == 'g' && cmd[4] == 'r' && cmd[5] == 'o' && cmd[6] == 'u' && cmd[7] == 'p' && cmd[8] == 's' && cmd[9] == ' ') {
            extern int current_user_idx;
            extern User user_table[MAX_USERS];
            extern int user_count;
            if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
                print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
            int i = 10;
            while (cmd[i] == ' ') i++;
            char username[MAX_NAME_LENGTH];
            int u = 0;
            while (cmd[i] && cmd[i] != ' ' && u < MAX_NAME_LENGTH-1) username[u++] = cmd[i++];
            username[u] = 0;
            while (cmd[i] == ' ') i++;
            if (!cmd[i]) {
                print_string("Usage: setgroups <username> <groupmask>", -1, video, cursor, COLOR_YELLOW);
                return;
            }
            int groupmask = 0;
            // Parse groupmask as integer (supports hex with 0x prefix)
            if (cmd[i] == '0' && (cmd[i+1] == 'x' || cmd[i+1] == 'X')) {
                i += 2;
                while (cmd[i]) {
                    char c = cmd[i];
                    if (c >= '0' && c <= '9') groupmask = (groupmask << 4) | (c - '0');
                    else if (c >= 'a' && c <= 'f') groupmask = (groupmask << 4) | (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') groupmask = (groupmask << 4) | (c - 'A' + 10);
                    else break;
                    i++;
                }
            } else {
                while (cmd[i] >= '0' && cmd[i] <= '9') {
                    groupmask = groupmask * 10 + (cmd[i] - '0');
                    i++;
                }
            }
            int found = 0;
            for (int j = 0; j < user_count; j++) {
                if (mini_strcmp(username, user_table[j].username) == 0) {
                    user_table[j].groups = groupmask;
                    found = 1;
                    print_string("Groups updated.", -1, video, cursor, COLOR_LIGHT_GREEN);
                    break;
                }
            }
            if (!found) print_string("User not found.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
    // Debug: print password hash for a user
    if (cmd[0] == 'd' && cmd[1] == 'e' && cmd[2] == 'b' && cmd[3] == 'u' && cmd[4] == 'g' && cmd[5] == 'h' && cmd[6] == 'a' && cmd[7] == 's' && cmd[8] == 'h' && cmd[9] == ' ') {
        char username[MAX_NAME_LENGTH];
        int i = 10, u = 0;
        while (cmd[i] == ' ') i++;
        while (cmd[i] && u < MAX_NAME_LENGTH-1) username[u++] = cmd[i++];
        username[u] = 0;
        extern User user_table[MAX_USERS];
        extern int user_count;
        int found = 0;
        for (int j = 0; j < user_count; j++) {
            if (mini_strcmp(username, user_table[j].username) == 0) {
                found = 1;
                char hex[3*HASH_SIZE+1];
                int h = 0;
                for (int k = 0; k < HASH_SIZE; k++) {
                    unsigned char byte = user_table[j].password_hash[k];
                    const char* hexchars = "0123456789ABCDEF";
                    hex[h++] = hexchars[(byte >> 4) & 0xF];
                    hex[h++] = hexchars[byte & 0xF];
                    hex[h++] = ' ';
                }
                hex[h] = 0;
                print_string("Password hash: ", -1, video, cursor, COLOR_YELLOW);
                print_string(hex, -1, video, cursor, COLOR_YELLOW);
                return;
            }
        }
        if (!found) print_string("User not found.", -1, video, cursor, COLOR_LIGHT_RED);
        return;
    }


    // --- Shell Script Detection and Loading ---
    // If the command is a filename ending with .sh and is a file, treat as script
    // --- Script Detection: check if first word ends with .sh ---
    char first_word[MAX_NAME_LENGTH];
    int fw_i = 0;
    while (cmd[fw_i] && cmd[fw_i] != ' ' && fw_i < MAX_NAME_LENGTH - 1) { first_word[fw_i] = cmd[fw_i]; fw_i++; }
    first_word[fw_i] = 0;
    int fwlen = fw_i;

    // --- Variable Assignment ---
    int eq_pos = -1;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '=') { eq_pos = i; break; }
        if (cmd[i] == ' ' || cmd[i] == '\t') break;
    }
    if (eq_pos > 0 && eq_pos < MAX_VAR_NAME - 1) {
        int valid = ((cmd[0] >= 'A' && cmd[0] <= 'Z') || (cmd[0] >= 'a' && cmd[0] <= 'z') || cmd[0] == '_');
        for (int i = 1; i < eq_pos && valid; i++) {
            if (!((cmd[i] >= 'A' && cmd[i] <= 'Z') || (cmd[i] >= 'a' && cmd[i] <= 'z') || (cmd[i] >= '0' && cmd[i] <= '9') || cmd[i] == '_')) valid = 0;
        }
        if (valid) {
            char varname[MAX_VAR_NAME];
            char varval[MAX_VAR_VALUE];
            for (int i = 0; i < eq_pos && i < MAX_VAR_NAME - 1; i++) varname[i] = cmd[i];
            varname[eq_pos] = 0;
            int vi = 0;
            for (int i = eq_pos + 1; cmd[i] && vi < MAX_VAR_VALUE - 1; i++) varval[vi++] = cmd[i];
            varval[vi] = 0;
            set_var(varname, varval);
            print_string("[variable set]", -1, video, cursor, COLOR_LIGHT_GREEN);
            return;
        }
    }

    // --- Variable Substitution ---
    char substituted[MAX_CMD_BUFFER];
    substitute_vars(cmd, substituted, MAX_CMD_BUFFER);
    cmd = substituted;

    // --- Script Detection (first word ends with .sh) ---
    if (fwlen > 3 && first_word[fwlen-3] == '.' && first_word[fwlen-2] == 's' && first_word[fwlen-1] == 'h') {
        // Parse arguments: cmd = "script.sh arg1 arg2 ..."
        char script_name[MAX_NAME_LENGTH];
        int ci = 0, si = 0;
        // Copy script name (up to first space)
        while (cmd[ci] && cmd[ci] != ' ' && ci < MAX_NAME_LENGTH - 1) script_name[si++] = cmd[ci++];
        script_name[si] = 0;
        // Parse up to 9 arguments
        script_argc = 0;
        while (cmd[ci] == ' ') ci++;
        for (int argn = 0; argn < MAX_SCRIPT_ARGS && cmd[ci]; argn++) {
            int ai = 0;
            while (cmd[ci] && cmd[ci] != ' ' && ai < MAX_VAR_VALUE - 1) script_args[argn][ai++] = cmd[ci++];
            script_args[argn][ai] = 0;
            script_argc++;
            while (cmd[ci] == ' ') ci++;
        }
        {
            char content[MAX_FILE_CONTENT];
            int size = shell_read_file_content(script_name, content, (int)sizeof(content));
            if (size >= 0) {
            // Read file content and execute each line as a shell command
            int line_start = 0;
            for (int i = 0; i <= size; i++) {
                if (content[i] == '\n' || content[i] == 0) {
                    char line[MAX_CMD_BUFFER];
                    int len = i - line_start;
                    if (len > 0 && len < MAX_CMD_BUFFER) {
                        // Copy line
                        for (int j = 0; j < len; j++) line[j] = content[line_start + j];
                        line[len] = 0;
                        // --- Trim leading and trailing whitespace ---
                        int l = 0;
                        while (line[l] == ' ' || line[l] == '\t') l++;
                        int r = len - 1;
                        while (r >= l && (line[r] == ' ' || line[r] == '\t')) r--;
                        int trimmed_len = r - l + 1;
                        if (trimmed_len > 0) {
                            char trimmed[MAX_CMD_BUFFER];
                            for (int t = 0; t < trimmed_len; t++) trimmed[t] = line[l + t];
                            trimmed[trimmed_len] = 0;
                            // Skip blank and comment lines
                            int is_blank = 1;
                            for (int k = 0; k < trimmed_len; k++) {
                                if (trimmed[k] != ' ' && trimmed[k] != '\t') { is_blank = 0; break; }
                            }
                            if (!is_blank && trimmed[0] != '#') {
                                // --- Variable and argument substitution for each script line ---
                                char substituted[MAX_CMD_BUFFER];
                                substitute_vars(trimmed, substituted, MAX_CMD_BUFFER);
                                dispatch_command(substituted, video, cursor);
                            }
                        }
                    }
                    line_start = i + 1;
                }
            }
            print_string("[script finished]", -1, video, cursor, COLOR_LIGHT_GREEN);
            // Clear script args after script finishes
            script_argc = 0;
            for (int i = 0; i < MAX_SCRIPT_ARGS; i++) script_args[i][0] = 0;
            return;
            }
        }
    }



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
        char filename[MAX_PATH_LENGTH];
        char resolved[MAX_PATH_LENGTH];
        int fn = 0;
        while (cmd[start] && fn < MAX_PATH_LENGTH - 1) filename[fn++] = cmd[start++];
        filename[fn] = 0;
        if (!shell_resolve_required_path(filename, resolved, "Usage: chmod <filename>", video, cursor)) {
            return;
        }
        FInode inode;
        int inode_num = path_resolve(resolved, &inode);
        if (inode_num < 0) {
            print_string("File not found.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (inode.uid != (uint16_t)current_user_idx && !IS_EFFECTIVE_ADMIN(current_user_idx)) {
            // Allow group members with group write permission to change permissions
            if ((user_table[current_user_idx].groups & inode.gid) != 0 && (inode.mode & INODE_PERM_GROUP_W) != 0) {
                // allowed
            } else {
                print_string("Permission denied: only owner, group (with write), or admin can change permissions.", -1, video, cursor, COLOR_LIGHT_RED);
                return;
            }
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
        inode.mode = (inode.mode & 0xF000) | perms;
        if (disk_write_inode((uint32_t)inode_num, &inode) != 0) {
            print_string("Permission update failed.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        print_string("Permissions updated.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    // chown: admin only, new format: chown <filename>
    if (cmd[0] == 'c' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'w' && cmd[4] == 'n' && cmd[5] == ' ') {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
            print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        int start = 6;
        while (cmd[start] == ' ') start++;
        char filename[MAX_PATH_LENGTH];
        char resolved[MAX_PATH_LENGTH];
        int fn = 0;
        while (cmd[start] && fn < MAX_PATH_LENGTH - 1) filename[fn++] = cmd[start++];
        filename[fn] = 0;
        if (!shell_resolve_required_path(filename, resolved, "Usage: chown <filename>", video, cursor)) {
            return;
        }
        FInode inode;
        int inode_num = path_resolve(resolved, &inode);
        if (inode_num < 0) {
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
        inode.uid = (uint16_t)owner_idx;
        if (disk_write_inode((uint32_t)inode_num, &inode) != 0) {
            print_string("Owner update failed.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        print_string("Owner updated.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    // edituser command
    if (mini_strcmp(cmd, "edituser") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        int target_idx = -1;
        if (current_user_idx < 0) {
            print_string("Not logged in.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (IS_EFFECTIVE_ADMIN(current_user_idx)) {
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
        hash_password(new_password, user_table[target_idx].password_hash);
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
            buf[n++] = IS_EFFECTIVE_ADMIN(i) ? 'A' : 'U';
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
        if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
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
        hash_password(password, user_table[user_count].password_hash);
        user_table[user_count].groups = GROUP_USERS; // Default group
        user_count++;
        print_string("User added.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }

    // Admin-only: deluser
    if (mini_strcmp(cmd, "deluser") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        extern void shell_read_line(char* prompt, char* buf, int max_len, char* video, int* cursor);
        if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
            print_string("Access denied: admin only.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        char username[MAX_NAME_LENGTH];
        shell_read_line("Delete username: ", username, MAX_NAME_LENGTH, video, cursor);
        int idx = -1;
        int admin_count = 0;
        for (int i = 0; i < user_count; i++) {
            if (IS_EFFECTIVE_ADMIN(i)) admin_count++;
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
        if (IS_EFFECTIVE_ADMIN(idx) && admin_count <= 1) {
            print_string("Cannot delete last admin.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        // Shift users
        for (int i = idx; i < user_count-1; i++) user_table[i] = user_table[i+1];
        user_count--;
        print_string("User deleted.", -1, video, cursor, COLOR_LIGHT_GREEN);
        return;
    }
    // Admin-only command: listusers
    if (mini_strcmp(cmd, "listusers") == 0) {
        extern int current_user_idx;
        extern User user_table[MAX_USERS];
        extern int user_count;
        if (current_user_idx < 0 || !IS_EFFECTIVE_ADMIN(current_user_idx)) {
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
        char filename[MAX_PATH_LENGTH];
        char resolved[MAX_PATH_LENGTH];
        int fn = 0;
        while (cmd[start] && fn < MAX_PATH_LENGTH - 1) filename[fn++] = cmd[start++];
        filename[fn] = 0;
        if (!shell_resolve_required_path(filename, resolved, "Usage: edit <file>", video, cursor)) {
            return;
        }
        extern int current_user_idx;
        FInode inode;
        int inode_num = path_resolve(resolved, &inode);
        if (inode_num < 0) {
            print_string("File not found", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        if (current_user_idx < 0 || (inode.uid != (uint16_t)current_user_idx && !IS_EFFECTIVE_ADMIN(current_user_idx))) {
            print_string("Permission denied.", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }
        nano_editor(resolved, video, cursor);
        return;
    }
    
    add_to_history(cmd);
    
    if (mini_strcmp(cmd, "pwd") == 0) {
        handle_pwd_command(video, cursor);
    } else if (mini_strcmp(cmd, "savedir") == 0) {
        handle_savedir_command("", video, cursor);
    } else if (cmd[0] == 's' && cmd[1] == 'a' && cmd[2] == 'v' && cmd[3] == 'e' && cmd[4] == 'd' && cmd[5] == 'i' && cmd[6] == 'r' && cmd[7] == ' ') {
        handle_savedir_command(cmd + 8, video, cursor);
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
        handle_cat_command(cmd + 4, video, cursor, COLOR_LIGHT_GRAY);
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
        char filename[MAX_PATH_LENGTH];
        int fn = 0;
        int fi = gt + 1;
        while (cmd[fi]) {
            if (cmd[fi] != ' ' && fn < MAX_PATH_LENGTH - 1) filename[fn++] = cmd[fi];
            fi++;
        }
        filename[fn] = 0;
        {
            char target_path[MAX_PATH_LENGTH];
            if (!shell_prepare_create_path(filename, target_path, MAX_PATH_LENGTH)) {
                print_string("Bad echo syntax", 16, video, cursor, 0x0C);
                return;
            }
            handle_echo_command(text, target_path, video, cursor, 0x0A);
        }
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
                print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
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
            print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
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
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

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
    } else if (cmd[0] == 'n' && cmd[1] == 'e' && cmd[2] == 't' && cmd[3] == ' ' && cmd[4] == 'p' && cmd[5] == 'u' && cmd[6] == 'm' && cmd[7] == 'p' && cmd[8] == ' ') {
        int count = 0;
        int processed = 0;
        int no_data = 0;
        int errors = 0;
        char line[96];
        char value[24];

        if (!parse_nonneg_int(cmd + 9, &count) || count <= 0) {
            print_string("Usage: net pump <count>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        for (int i = 0; i < count; i++) {
            int r = net_poll_once();
            if (r == 1) processed++;
            else if (r == 0) no_data++;
            else if (r < 0) errors++;
        }

        line[0] = 0;
        str_concat(line, "NET pump: processed=");
        int_to_str(processed, value); str_concat(line, value);
        str_concat(line, " no-data=");
        int_to_str(no_data, value); str_concat(line, value);
        str_concat(line, " err=");
        int_to_str(errors, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
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
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);

        line[0] = 0;
        str_concat(line, "TCP syn=");
        int_to_str((int)stats.syn_received, value); str_concat(line, value);
        str_concat(line, " synack=");
        int_to_str((int)stats.synack_sent, value); str_concat(line, value);
        str_concat(line, " est=");
        int_to_str((int)stats.established, value); str_concat(line, value);
        str_concat(line, " ack=");
        int_to_str((int)stats.ack_sent, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);

        line[0] = 0;
        str_concat(line, "TCP listen=");
        if (tcp_get_listen_port(&listen_port)) {
            str_concat(line, "on:");
            int_to_str((int)listen_port, value); str_concat(line, value);
        } else {
            str_concat(line, "off");
        }
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
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
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
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
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);

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
            print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
        }
    } else if (mini_strcmp(cmd, "sock") == 0) {
        print_string("SOCK usage: sock open udp|bind|send|recv|close|list", -1, video, cursor, COLOR_YELLOW);
    } else if (cmd[0] == 'u' && cmd[1] == 'd' && cmd[2] == 'p' && cmd[3] == 'e' && cmd[4] == 'c' && cmd[5] == 'h' && cmd[6] == 'o' && cmd[7] == ' ' && cmd[8] == 's' && cmd[9] == 't' && cmd[10] == 'a' && cmd[11] == 'r' && cmd[12] == 't' && cmd[13] == ' ') {
        int port = 0;
        char line[64];
        char value[24];
        int fd;

        if (!parse_nonneg_int(cmd + 14, &port) || port < 1 || port > 65535) {
            print_string("Usage: udpecho start <port>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (udpecho_fd >= 0) {
            sock_close(udpecho_fd);
            udpecho_fd = -1;
            udpecho_port = 0;
        }

        fd = sock_open_udp();
        if (fd < 0 || sock_bind(fd, (uint16_t)port) <= 0) {
            if (fd >= 0) sock_close(fd);
            print_string("UDPECHO: start failed", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        udpecho_fd = fd;
        udpecho_port = (uint16_t)port;

        line[0] = 0;
        str_concat(line, "UDPECHO: started on port ");
        int_to_str(port, value); str_concat(line, value);
        print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
    } else if (mini_strcmp(cmd, "udpecho step") == 0) {
        int r = udpecho_step_once(video, cursor);
        if (r == -1) {
            print_string("UDPECHO: not running", -1, video, cursor, COLOR_YELLOW);
        } else if (r == 0) {
            print_string("UDPECHO: no data", -1, video, cursor, COLOR_YELLOW);
        } else if (r < 0) {
            print_string("UDPECHO: step failed", -1, video, cursor, COLOR_LIGHT_RED);
        }
    } else if (cmd[0] == 'u' && cmd[1] == 'd' && cmd[2] == 'p' && cmd[3] == 'e' && cmd[4] == 'c' && cmd[5] == 'h' && cmd[6] == 'o' && cmd[7] == ' ' && cmd[8] == 'r' && cmd[9] == 'u' && cmd[10] == 'n' && cmd[11] == ' ') {
        int count = 0;
        int echoed = 0;
        char line[64];
        char value[24];

        if (!parse_nonneg_int(cmd + 12, &count) || count <= 0) {
            print_string("Usage: udpecho run <count>", -1, video, cursor, COLOR_LIGHT_RED);
            return;
        }

        if (udpecho_fd < 0) {
            print_string("UDPECHO: not running", -1, video, cursor, COLOR_YELLOW);
            return;
        }

        for (int i = 0; i < count; i++) {
            int r = udpecho_step_once(video, cursor);
            if (r > 0) echoed++;
        }

        line[0] = 0;
        str_concat(line, "UDPECHO: echoed ");
        int_to_str(echoed, value); str_concat(line, value);
        str_concat(line, " packets");
        print_string(line, -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "udpecho stop") == 0) {
        if (udpecho_fd >= 0) {
            sock_close(udpecho_fd);
            udpecho_fd = -1;
            udpecho_port = 0;
            print_string("UDPECHO: stopped", -1, video, cursor, COLOR_YELLOW);
        } else {
            print_string("UDPECHO: not running", -1, video, cursor, COLOR_YELLOW);
        }
    } else if (mini_strcmp(cmd, "udpecho status") == 0) {
        char line[64];
        char value[24];
        if (udpecho_fd < 0) {
            print_string("UDPECHO: OFF", -1, video, cursor, COLOR_YELLOW);
        } else {
            line[0] = 0;
            str_concat(line, "UDPECHO: ON fd=");
            int_to_str(udpecho_fd, value); str_concat(line, value);
            str_concat(line, " port=");
            int_to_str((int)udpecho_port, value); str_concat(line, value);
            print_string(line, -1, video, cursor, COLOR_LIGHT_GREEN);
        }
    } else if (mini_strcmp(cmd, "udpecho") == 0) {
        print_string("UDPECHO usage: udpecho start|step|run|stop|status", -1, video, cursor, COLOR_YELLOW);
    } else if (cmd[0] == 'p' && cmd[1] == 'k' && cmd[2] == 'g' && cmd[3] == ' ' && cmd[4] == 'i' && cmd[5] == 'n' && cmd[6] == 's' && cmd[7] == 't' && cmd[8] == 'a' && cmd[9] == 'l' && cmd[10] == 'l' && cmd[11] == ' ') {
        handle_pkg_install_command(cmd + 12, video, cursor);
    } else if (mini_strcmp(cmd, "pkg search") == 0) {
        handle_pkg_search_command(video, cursor);
    } else if (mini_strcmp(cmd, "pkg repo") == 0) {
        handle_pkg_repo_command("", video, cursor);
    } else if (cmd[0] == 'p' && cmd[1] == 'k' && cmd[2] == 'g' && cmd[3] == ' ' && cmd[4] == 'r' && cmd[5] == 'e' && cmd[6] == 'p' && cmd[7] == 'o' && cmd[8] == ' ') {
        handle_pkg_repo_command(cmd + 9, video, cursor);
    } else if (mini_strcmp(cmd, "pkg list") == 0) {
        handle_pkg_list_command(video, cursor);
    } else if (cmd[0] == 'p' && cmd[1] == 'k' && cmd[2] == 'g' && cmd[3] == ' ' && cmd[4] == 'r' && cmd[5] == 'e' && cmd[6] == 'm' && cmd[7] == 'o' && cmd[8] == 'v' && cmd[9] == 'e' && cmd[10] == ' ') {
        handle_pkg_remove_command(cmd + 11, video, cursor);
    } else if (mini_strcmp(cmd, "pkg") == 0) {
        print_string("PKG usage: pkg repo|search|install|list|remove", -1, video, cursor, COLOR_YELLOW);
    } else if (mini_strcmp(cmd, "about") == 0) {
        handle_command(cmd, video, cursor, "about", "Smiggles OS is a lightweight operating system designed by Jules Miller and Vajra Vanukuri.", COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help") == 0) {
        print_string(
            "--- Commands ---\n"
            "ls - list directory\n"
            "cd <dir> - change directory\n"
            "savedir [<dir>|clear] - default create dir\n"
            "edit <file> - open text editor\n"
            "cat <file> - read file\n"
            "touch <file> - create file\n"
            "mkdir <dir> - create directory\n"
            "rm <path> - remove file\n"
            "mv <old> <new> - move or rename\n"
            "echo \"text\" > <file> - write file\n"
            "clear - clear screen\n"
            "time - show date/time\n"
            "whoami - current user\n"
            "login - log in\n"
            "reboot - restart\n"
            "halt - shutdown\n"
            "\n"
            "More: help net | help pkg | help admin | help dev",
            -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help net") == 0) {
        print_string(
            "--- Networking ---\n"
            "pciscan - detect RTL8139\n"
            "arp setip <a.b.c.d> - set local IP\n"
            "arp table - show ARP cache\n"
            "ping <a.b.c.d> - ICMP echo\n"
            "net pump <count> - poll network stack\n"
            "tcp listen <port>|off|show - TCP listener\n"
            "tcp stats - TCP counters\n"
            "nettest - networking subsystem smoke checks\n"
            "sock open udp|bind|send|recv|close|list - UDP socket tools\n"
            "udpecho start|step|run|stop|status - UDP echo server",
            -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help pkg") == 0) {
        print_string(
            "--- Packages ---\n"
            "pkg repo <ip> <port> - set repository\n"
            "pkg search - list available packages\n"
            "pkg install <name> [path] - install package\n"
            "pkg list - list installed packages\n"
            "pkg remove <name> - uninstall package",
            -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help admin") == 0) {
        print_string(
            "--- Users/Admin ---\n"
            "whoami | login | logout\n"
            "adduser | deluser | edituser\n"
            "creategroup | delgroup | listgroups | lsgroup\n"
            "whois <user> | setgroups <user> <mask>\n"
            "chown <file>\n"
            "tz show|set <zone>",
            -1, video, cursor, COLOR_LIGHT_GRAY);
    } else if (mini_strcmp(cmd, "help dev") == 0) {
        print_string(
            "--- Dev/Debug ---\n"
            "ver | uptime | neofetch\n"
            "log | log show [n] | log level [name|0-3] | log clear | log test\n"
            "dmesg - shortcut for log show 25\n"
            "basic | exec <file.bas>\n"
            "syscalltest | nettest\n"
            "fdtest <file> | memtest | pathtest | proctest | spawn ring3\n"
            "free | df | fscheck\n"
            "panic",
            -1, video, cursor, COLOR_LIGHT_GRAY);
    


    } else if (mini_strcmp(cmd, "basic") == 0) {
        basic_repl(video, cursor);

    } else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c' && cmd[4] == ' ') {
        int start = 5;
        char filename[MAX_PATH_LENGTH];
        char resolved[MAX_PATH_LENGTH];
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
            if (!shell_resolve_required_path(filename, resolved, "Usage: exec <file.bas>", video, cursor)) {
                return;
            }
            basic_run_file(resolved, video, cursor);
        }

    } else if (is_math_expr(cmd)) {
        handle_calc_command(cmd, video, cursor);
    } else if (mini_strcmp(cmd, "lsall") == 0) {
        handle_lsall_command(video, cursor);
    } else if (mini_strcmp(cmd, "time") == 0) {
        handle_time_command(video, cursor, 0x0A);
    } else if (mini_strcmp(cmd, "tz") == 0) {
        handle_timezone_command("", video, cursor);
    } else if (cmd[0] == 't' && cmd[1] == 'z' && cmd[2] == ' ') {
        handle_timezone_command(cmd + 2, video, cursor);
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
    } else if (mini_strcmp(cmd, "log") == 0) {
        handle_log_command("", video, cursor);
    } else if (cmd[0] == 'l' && cmd[1] == 'o' && cmd[2] == 'g' && cmd[3] == ' ') {
        handle_log_command(cmd + 4, video, cursor);
    } else if (mini_strcmp(cmd, "dmesg") == 0) {
        handle_log_show_command("25", video, cursor);
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
    } else if (mini_strcmp(cmd, "nettest") == 0) {
        handle_nettest_command(video, cursor);
    } else if (mini_strcmp(cmd, "memtest") == 0) {
        handle_memtest_command(video, cursor);
    } else if (mini_strcmp(cmd, "pathtest") == 0) {
        handle_pathtest_command(video, cursor);
    } else if (mini_strcmp(cmd, "proctest") == 0) {
        handle_proctest_command(video, cursor);
    } else if (mini_strcmp(cmd, "halt") == 0) {
        handle_halt_command(video, cursor);
    } else if (mini_strcmp(cmd, "panic") == 0) {
        handle_panic_command();
    } else if (mini_strcmp(cmd, "reboot") == 0) {
        handle_reboot_command();
    } else if (cmd[0] == 'f' && cmd[1] == 'i' && cmd[2] == 'l' && cmd[3] == 'e' && cmd[4] == 's' && cmd[5] == 'i' && cmd[6] == 'z' && cmd[7] == 'e' && cmd[8] == ' ') {
        handle_filesize_command(cmd + 9, video, cursor);
    } else if (mini_strcmp(cmd,"filesize")==0){
        print_string("Usage: filesize <path>", -1, video, cursor, COLOR_YELLOW);
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
        "udp", "tcp", "net", "sock", "udpecho", "pkg", "about", "ver", "panic", "halt", "reboot", "history", "df", "fscheck", "free", "uptime", "log", "dmesg", "filesize", "neofetch", "basic", "syscalltest", "nettest", "memtest", "pathtest", "proctest", "fdtest", "spawn", "ps", "kill", "wait", "savedir"
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
    {
        DirectoryEntry entries[32];
        int entry_count = vfs_readdir(newfs_cwd, entries, 32);
        if (entry_count < 0) entry_count = 0;
        for (int i = 0; i < entry_count && tab_match_count < 32; i++) {
            int match = 1;
            if (entries[i].name_len <= 0) continue;
            for (int j = 0; j < partial_len && j < entries[i].name_len; j++) {
                if (entries[i].name[j] != partial[j]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                str_copy(tab_matches[tab_match_count], entries[i].name, MAX_NAME_LENGTH);
                if (entries[i].file_type == DIRENT_TYPE_DIR) {
                    int len = str_len(tab_matches[tab_match_count]);
                    if (len < MAX_NAME_LENGTH - 1) {
                        tab_matches[tab_match_count][len] = '/';
                        tab_matches[tab_match_count][len + 1] = 0;
                    }
                }
                tab_match_count++;
            }
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