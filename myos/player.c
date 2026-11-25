#include "kernel.h"

int vfs_open(const char* path, int flags);
int vfs_write(int fd, const char* buf, int count);
int vfs_close(int fd);
void sb16_set_sample_rate(uint32_t rate);
void sb16_set_stream_rate(uint32_t rate);

typedef struct {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];
    char subchunk1_id[4];
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char subchunk2_id[4];
    uint32_t subchunk2_size;
} __attribute__((packed)) dbg_wav_header_t;

static void dbg_print_num(int num, int x, int y, unsigned char color) {
    char* video = display_text_buffer();
    int idx = (y * 80 + x) * 2;
    if (num < 0) {
        video[idx] = '-';
        video[idx + 1] = color;
        num = -num;
        idx += 2;
    }
    char buf[12];
    int i = 0;
    if (num == 0) buf[i++] = '0';
    while (num > 0 && i < 10) {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }
    while (i > 0) {
        video[idx] = buf[--i];
        video[idx + 1] = color;
        idx += 2;
    }
    display_flush_framebuffer();
}

static void dbg_print_str(const char* str, int x, int y, unsigned char color) {
    char* video = display_text_buffer();
    int idx = (y * 80 + x) * 2;
    while (*str) {
        video[idx] = *str++;
        video[idx + 1] = color;
        idx += 2;
    }
    display_flush_framebuffer();
}

void play_wav_file_resolved(const char* resolved_path) {
    char* video = display_text_buffer();
    for (int cell = 16 * 80 * 2; cell < 20 * 80 * 2; cell += 2) {
        video[cell] = ' ';
        video[cell + 1] = 0x07;
    }
    dbg_print_str("File FD Hook: ", 2, 16, 0x0E);
    int file_fd = (int)syscall_invoke2(SYS_OPEN, (unsigned int)resolved_path, (unsigned int)FS_O_READ);
    dbg_print_num(file_fd, 16, 16, (file_fd < 0) ? 0x0C : 0x0A);
    dbg_print_str("opening file", 0, 10, 0x0E);
    dbg_print_num(file_fd, 20, 10, (file_fd < 0) ? 0x0C : 0x0A);
    if (file_fd < 0) return;
    dbg_print_str("opening audio", 0, 11, 0x0E);
    int audio_fd = vfs_open("/dev/sound", 0);
    dbg_print_num(audio_fd, 20, 11, (audio_fd < 0) ? 0x0C : 0x0A);
    if (audio_fd < 0) {
        syscall_invoke1(SYS_CLOSE, (unsigned int)file_fd);
        return;
    }
    dbg_wav_header_t header;
    dbg_print_str("reading header", 0, 12, 0x0E);
    int h_read = (int)syscall_invoke3(SYS_READ, (unsigned int)file_fd, (unsigned int)&header, (unsigned int)sizeof(dbg_wav_header_t));
    dbg_print_num(h_read, 20, 12, (h_read <= 0) ? 0x0C : 0x0A);
    char chunk_id[4];
    uint32_t chunk_size;
    for (int i = 0; i < 4; i++) {
        chunk_id[i] = header.subchunk2_id[i];
    }

    chunk_size = header.subchunk2_size;
    while (chunk_id[0] != 'd' || chunk_id[1] != 'a' ||
        chunk_id[2] != 't' || chunk_id[3] != 'a') {
        uint8_t skip_buf[128];

        while (chunk_size > 0) {
            int skip = chunk_size > sizeof(skip_buf) ? sizeof(skip_buf) : chunk_size;
            int skipped = (int)syscall_invoke3(
                SYS_READ, (unsigned int)file_fd,
                (unsigned int)skip_buf, (unsigned int)skip
            );

            if (skipped <= 0) {
                dbg_print_str("wav chunk error", 0, 19, 0x0C);
                goto cleanup;
            }

            chunk_size -= skipped;
        }

        if (syscall_invoke3(
            SYS_READ, (unsigned int)file_fd,
            (unsigned int)chunk_id, 4
        ) != 4) {
            goto cleanup;
        }

        if (syscall_invoke3(
            SYS_READ, (unsigned int)file_fd,
            (unsigned int)&chunk_size, 4
        ) != 4) {
            goto cleanup;
        }
    }
    uint32_t total_bytes = chunk_size;
        dbg_print_str("sample rate", 0, 14, 0x0E);
        dbg_print_num(header.sample_rate, 20, 14, 0x0A);
        dbg_print_str("byte rate", 0, 15, 0x0E);
        dbg_print_num(header.byte_rate, 20, 15, 0x0A);
        dbg_print_str("data bytes", 0, 17, 0x0E);
        dbg_print_num(header.subchunk2_size, 20, 17, 0x0A);
        if (header.sample_rate > 0) {
            sb16_set_sample_rate(header.sample_rate);
        }
    uint32_t byte_rate = header.byte_rate;
    if (byte_rate == 0) byte_rate = header.sample_rate * 2;
    if (byte_rate == 0) byte_rate = 16000;
    sb16_set_stream_rate(byte_rate);
    if (total_bytes == 0 || total_bytes > 20000000) total_bytes = 4000000;
    uint32_t total_seconds = total_bytes / byte_rate;
    uint32_t total_min = total_seconds / 60;
    uint32_t total_sec = total_seconds % 60;
    static char chunk_buf[32768];
    int bytes_read;
    uint32_t accumulated_bytes = 0;
    int last_sec = -1;
    int last_min = -1;
    int last_filled = -1;
    while ((bytes_read = (int)syscall_invoke3(SYS_READ, (unsigned int)file_fd, (unsigned int)chunk_buf, (unsigned int)sizeof(chunk_buf))) > 0) {
        dbg_print_str("writing audio", 0, 13, 0x0E);
        int written = vfs_write(audio_fd, chunk_buf, bytes_read);
        dbg_print_num(written, 20, 13, (written < 0) ? 0x0C : 0x0A);
        if (written < 0) break;
        accumulated_bytes += bytes_read;
        uint32_t cur_seconds = accumulated_bytes / byte_rate;
        uint32_t cur_min = cur_seconds / 60;
        uint32_t cur_sec = cur_seconds % 60;
        if ((int)cur_sec != last_sec || (int)cur_min != last_min) {
            dbg_print_num(cur_min, 2, 18, 0x0B);
            dbg_print_str(":", 3, 18, 0x0B);
            if (cur_sec < 10) dbg_print_str("0", 4, 18, 0x0B);
            dbg_print_num(cur_sec, (cur_sec < 10) ? 5 : 4, 18, 0x0B);
            dbg_print_str(" / ", 6, 18, 0x07);
            dbg_print_num(total_min, 9, 18, 0x0F);
            dbg_print_str(":", 10, 18, 0x0F);
            if (total_sec < 10) dbg_print_str("0", 11, 18, 0x0F);
            dbg_print_num(total_sec, (total_sec < 10) ? 12 : 11, 18, 0x0F);
            last_sec = cur_sec;
            last_min = cur_min;
        }
        int bar_max_chars = 50;
        int filled_chars = (accumulated_bytes * bar_max_chars) / total_bytes;
        if (filled_chars > bar_max_chars) filled_chars = bar_max_chars;
        if (filled_chars != last_filled) {
            int bar_start_x = 16;
            int v_idx = (18 * 80 + bar_start_x) * 2;
            for (int b = 0; b < bar_max_chars; b++) {
                if (b < filled_chars) {
                    video[v_idx] = 0xDB;
                    video[v_idx + 1] = 0x0C;
                } else if (b == filled_chars) {
                    video[v_idx] = 'O';
                    video[v_idx + 1] = 0x0F;
                } else {
                    video[v_idx] = '-';
                    video[v_idx + 1] = 0x08;
                }
                v_idx += 2;
            }
            last_filled = filled_chars;
            display_flush_framebuffer();
        }
    }
    cleanup:
        vfs_close(audio_fd);
        syscall_invoke1(SYS_CLOSE, (unsigned int)file_fd);
}
