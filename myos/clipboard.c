#include "kernel.h"

#define CLIPBOARD_CAPACITY MAX_FILE_CONTENT

static char clipboard_buf[CLIPBOARD_CAPACITY];
static int clipboard_len = 0;

static int clip_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void clipboard_clear(void) {
    clipboard_len = 0;
    clipboard_buf[0] = 0;
}

int clipboard_set_text_len(const char* text, int len) {
    if (!text || len <= 0) {
        clipboard_clear();
        return 0;
    }

    if (len >= CLIPBOARD_CAPACITY) {
        len = CLIPBOARD_CAPACITY - 1;
    }

    for (int i = 0; i < len; i++) {
        clipboard_buf[i] = text[i];
    }
    clipboard_buf[len] = 0;
    clipboard_len = len;
    return clipboard_len;
}

int clipboard_set_text(const char* text) {
    if (!text) {
        clipboard_clear();
        return 0;
    }
    return clipboard_set_text_len(text, str_len(text));
}

const char* clipboard_get_text(void) {
    return clipboard_buf;
}

int clipboard_get_length(void) {
    return clipboard_len;
}

int clipboard_has_text(void) {
    return clipboard_len > 0;
}

int clipboard_copy_word_at(const char* text, int len, int cursor_index) {
    int idx;
    int start;
    int end;

    if (!text || len <= 0) return 0;

    if (cursor_index < 0) cursor_index = 0;
    if (cursor_index > len) cursor_index = len;

    idx = cursor_index;
    if (idx == len && idx > 0) idx--;

    if (idx < len && clip_is_space(text[idx])) {
        while (idx < len && clip_is_space(text[idx])) idx++;
        if (idx >= len) {
            idx = cursor_index - 1;
            if (idx < 0 || clip_is_space(text[idx])) return 0;
        }
    }

    start = idx;
    while (start > 0 && !clip_is_space(text[start - 1])) start--;

    end = idx;
    while (end < len && !clip_is_space(text[end])) end++;

    if (end <= start) return 0;

    return clipboard_set_text_len(text + start, end - start);
}