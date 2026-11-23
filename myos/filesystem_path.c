#include "filesystem_new.h"

int fs_path_normalize(const char* in_path, char* out_path, int out_max) {
    if (!in_path || !out_path || out_max < 2) return 0;

    char segments[32][256];
    int segment_count = 0;
    int i = 0;

    while (in_path[i]) {
        while (in_path[i] == '/') i++;
        if (!in_path[i]) break;
        if (segment_count >= 32) return 0;

        int segment_len = 0;
        while (in_path[i] && in_path[i] != '/') {
            if (segment_len >= 255) return 0;
            segments[segment_count][segment_len++] = in_path[i++];
        }
        segments[segment_count][segment_len] = 0;

        if (segment_len == 1 && segments[segment_count][0] == '.') continue;
        if (segment_len == 2 &&
            segments[segment_count][0] == '.' &&
            segments[segment_count][1] == '.') {
            if (segment_count > 0) segment_count--;
            continue;
        }

        if (segment_count >= 32) return 0;
        segment_count++;
    }

    int out_len = 0;
    if (segment_count == 0) {
        out_path[0] = '/';
        out_path[1] = 0;
        return 1;
    }

    for (int s = 0; s < segment_count; s++) {
        int segment_len = 0;
        while (segments[s][segment_len]) segment_len++;
        if (out_len + 1 + segment_len >= out_max) return 0;
        out_path[out_len++] = '/';
        for (int k = 0; k < segment_len; k++) {
            out_path[out_len++] = segments[s][k];
        }
    }
    out_path[out_len] = 0;
    return 1;
}
