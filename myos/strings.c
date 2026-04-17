#include "kernel.h"

int str_len(const char* s) {
    int len = 0;
    if (!s) return 0;
    while (s[len] != '\0') len++;
    return len;
}

void str_copy(char* dst, const char* src, int max) {
    int i = 0;
    if (!dst || !src || max <= 0) return;
    for (; i < max - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

int str_equal(const char* a, const char* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int mini_strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void int_to_str(int value, char* buf) {
    char temp[32];
    int i = 0;
    int negative = 0;

    if (!buf) return;
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    if (value < 0) {
        negative = 1;
        value = -value;
    }

    while (value > 0 && i < (int)sizeof(temp) - 1) {
        temp[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    int pos = 0;
    if (negative) buf[pos++] = '-';
    while (i > 0) {
        buf[pos++] = temp[--i];
    }
    buf[pos] = '\0';
}

void str_concat(char* dest, const char* src) {
    int dest_len = str_len(dest);
    int i = 0;

    if (!dest || !src) return;
    while (src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i++;
    }
    dest[dest_len + i] = '\0';
}