#include "filesystem_new.h"

int fs_path_normalize(const char* in_path, char* out_path, int out_max) {
    if (!in_path || !out_path || out_max < 2) return 0;

    int out_len = 1;
    int seg_starts[32];
    int seg_count = 0;
    out_path[0] = '/';
    out_path[1] = 0;

    int i = 0;
    while (in_path[i]) {
        while (in_path[i] == '/') i++;
        if (!in_path[i]) break;

        char part[256];
        int part_len = 0;
        while (in_path[i] && in_path[i] != '/' && part_len < 255) {
            part[part_len++] = in_path[i++];
        }
        part[part_len] = 0;

        if (part_len == 0) continue;
        if (part_len == 1 && part[0] == '.') continue;

        if (part_len == 2 && part[0] == '.' && part[1] == '.') {
            if (seg_count > 0) {
                int popped_start = seg_starts[seg_count - 1];
                seg_count--;
                out_len = popped_start;
                if (out_len > 1 && out_path[out_len - 1] == '/') {
                    out_len--;
                }
                out_path[out_len] = 0;
                if (out_len == 0) {
                    out_len = 1;
                    out_path[0] = '/';
                    out_path[1] = 0;
                }
            }
            continue;
        }

        if (seg_count >= 32) return 0;
        if (out_len > 1) {
            if (out_len >= out_max - 1) return 0;
            out_path[out_len++] = '/';
        }

        seg_starts[seg_count++] = out_len;
        for (int k = 0; k < part_len; k++) {
            if (out_len >= out_max - 1) return 0;
            out_path[out_len++] = part[k];
        }
        out_path[out_len] = 0;
    }

    if (out_len == 0) {
        out_path[0] = '/';
        out_path[1] = 0;
    }

    return 1;
}
