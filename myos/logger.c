#include "kernel.h"

#define LOG_STORE_PATH "/.klog"

static int logger_min_level = LOG_LEVEL_INFO;

static int clamp_level(int level) {
    if (level < LOG_LEVEL_DEBUG) return LOG_LEVEL_DEBUG;
    if (level > LOG_LEVEL_ERROR) return LOG_LEVEL_ERROR;
    return level;
}

static int load_store(char* out, int max_len) {
    int fd;
    int len;

    if (!out || max_len <= 0) return -1;
    out[0] = 0;

    fd = vfs_open(LOG_STORE_PATH, FS_O_READ);
    if (fd < 0) return 0;
    len = vfs_read(fd, out, max_len - 1);
    vfs_close(fd);
    if (len < 0) return -1;
    out[len] = 0;
    return len;
}

static int save_store(const char* data, int len) {
    int fd = vfs_open(LOG_STORE_PATH, FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
    if (fd < 0) return -1;
    if (len > 0 && vfs_write(fd, data, len) != len) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);
    return 0;
}

static int parse_uint(const char* s, int* cursor, int stop_char) {
    int value = 0;
    int i = *cursor;
    int saw_digit = 0;

    while (s[i] && s[i] != stop_char) {
        if (s[i] < '0' || s[i] > '9') return -1;
        saw_digit = 1;
        value = value * 10 + (s[i] - '0');
        i++;
    }

    if (!saw_digit || s[i] != stop_char) return -1;
    *cursor = i + 1;
    return value;
}

static void trim_oldest_lines(char* buf, int* len, int incoming) {
    int max_len = MAX_FILE_CONTENT - 1;
    while (*len + incoming > max_len) {
        int cut = 0;
        while (cut < *len && buf[cut] != '\n') cut++;
        if (cut < *len) cut++;
        if (cut <= 0 || cut >= *len) {
            *len = 0;
            buf[0] = 0;
            return;
        }
        for (int i = 0; i < (*len - cut); i++) {
            buf[i] = buf[i + cut];
        }
        *len -= cut;
        buf[*len] = 0;
    }
}

static void copy_message(char* dst, const char* src, int max_len) {
    int i = 0;

    if (!dst || max_len <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }

    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void logger_init(void) {
    logger_min_level = LOG_LEVEL_INFO;
}

void log_set_level(int level) {
    logger_min_level = clamp_level(level);
}

int log_get_level(void) {
    logger_min_level = clamp_level(logger_min_level);
    return logger_min_level;
}

const char* log_level_name(int level) {
    if (level == LOG_LEVEL_DEBUG) return "DEBUG";
    if (level == LOG_LEVEL_INFO) return "INFO";
    if (level == LOG_LEVEL_WARN) return "WARN";
    if (level == LOG_LEVEL_ERROR) return "ERROR";
    return "UNKNOWN";
}

void log_write(int level, const char* message) {
    char line[LOGGER_MESSAGE_MAX + 40];
    char num[16];
    char store[MAX_FILE_CONTENT];
    int line_len = 0;
    int size;
    int store_len;

    level = clamp_level(level);
    logger_min_level = clamp_level(logger_min_level);
    if (level < logger_min_level) return;

    line[0] = 0;
    int_to_str((int)ticks, num);
    str_concat(line, num);
    str_concat(line, "|");
    int_to_str(level, num);
    str_concat(line, num);
    str_concat(line, "|");
    copy_message(num, "", sizeof(num));
    str_concat(line, message ? message : "");
    str_concat(line, "\n");

    while (line[line_len]) line_len++;

    store_len = load_store(store, (int)sizeof(store));
    if (store_len < 0) return;
    size = store_len;
    if (size < 0) size = 0;
    if (size > MAX_FILE_CONTENT - 1) size = MAX_FILE_CONTENT - 1;
    store[size] = 0;

    trim_oldest_lines(store, &size, line_len);

    for (int i = 0; i < line_len && size < MAX_FILE_CONTENT - 1; i++) {
        store[size++] = line[i];
    }
    store[size] = 0;
    save_store(store, size);
}

int log_count(void) {
    char store[MAX_FILE_CONTENT];
    int store_len;
    int count = 0;
    store_len = load_store(store, (int)sizeof(store));
    if (store_len < 0) return 0;
    for (int i = 0; i < store_len; i++) {
        if (store[i] == '\n') count++;
    }
    return count;
}

int log_get_entry(int oldest_index, LogEntry* out_entry) {
    char store[MAX_FILE_CONTENT];
    int store_len;
    int current = 0;
    int i = 0;
    if (!out_entry) return 0;
    if (oldest_index < 0) return 0;

    store_len = load_store(store, (int)sizeof(store));
    if (store_len < 0) return 0;

    while (i < store_len) {
        int line_start = i;
        int cursor;
        int tick_value;
        int level_value;
        int out_pos = 0;

        while (i < store_len && store[i] != '\n') i++;

        if (current == oldest_index) {
            cursor = line_start;
            tick_value = parse_uint(store, &cursor, '|');
            level_value = parse_uint(store, &cursor, '|');
            if (tick_value < 0 || level_value < 0) return 0;

            out_entry->tick = (uint32_t)tick_value;
            out_entry->level = (uint8_t)clamp_level(level_value);

            while (cursor < store_len && store[cursor] != '\n' && out_pos < LOGGER_MESSAGE_MAX - 1) {
                out_entry->message[out_pos++] = store[cursor++];
            }
            out_entry->message[out_pos] = 0;
            return 1;
        }

        current++;
        if (i < store_len && store[i] == '\n') i++;
    }

    return 0;
}

void log_clear(void) {
    save_store("", 0);
}
