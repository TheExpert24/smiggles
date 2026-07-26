#include "kernel.h"

#define SB_DSP_RESET  0x226
#define SB_DSP_READ   0x22A
#define SB_DSP_WRITE  0x22C
#define SB_DSP_STATUS 0x22C

static __attribute__((aligned(4096))) uint8_t dma_buffer[16384];
static int initial_skip_count = 0;

static void sb16_write_dsp(uint8_t val) {
    while (inb(SB_DSP_STATUS) & 0x80);
    outb(SB_DSP_WRITE, val);
}

static void isa_dma_setup_channel1(uint32_t buffer_phys, uint32_t length) {
    uint32_t count = length - 1;
    outb(0x0A, 0x04 | 1); // Mask Channel 1
    outb(0x0C, 0x00);     // Clear flip-flop
    outb(0x0B, 0x48 | 0x10 | 0x00 | 1); // Auto-Init Single Mode

    outb(0x02, buffer_phys & 0xFF);
    outb(0x02, (buffer_phys >> 8) & 0xFF);
    outb(0x83, (buffer_phys >> 16) & 0xFF);

    outb(0x03, count & 0xFF);
    outb(0x03, (count >> 8) & 0xFF);
    outb(0x0A, 0x00 | 1); // Unmask Channel 1
}

static int sb16_open(const char* path, int flags) {
    (void)path; (void)flags;
    outb(SB_DSP_RESET, 1);
    for (volatile int i = 0; i < 2000; i++);
    outb(SB_DSP_RESET, 0);
    
    volatile int timeout = 10000;
    while ((inb(0x22E) & 0x80) == 0 && timeout > 0) { timeout--; }
    if (inb(SB_DSP_READ) != 0xAA) return -1;

    sb16_write_dsp(0xD1); // Speaker ON
    initial_skip_count = 44; 
    return 0;
}

static int sb16_write(int backend_fd, const char* buf, int size) {
    (void)backend_fd;
    if (size <= 0) return size;

    int start_offset = 0;
    if (initial_skip_count > 0) {
        if (size >= initial_skip_count) {
            start_offset = initial_skip_count;
            initial_skip_count = 0;
        } else {
            initial_skip_count -= size;
            return size;
        }
    }

    int bytes_to_copy = size - start_offset;
    if (bytes_to_copy > 4096) bytes_to_copy = 4096;

    int dma_idx = 0;
    for (int i = start_offset; i < start_offset + (bytes_to_copy & ~1); i += 2) {
        int16_t sample16 = (int16_t)((uint8_t)buf[i] | ((uint8_t)buf[i + 1] << 8));
        dma_buffer[dma_idx++] = (uint8_t)((sample16 + 32768) >> 8);
    }

    if (dma_idx == 0) return size;

    isa_dma_setup_channel1((uint32_t)dma_buffer, dma_idx);

    sb16_write_dsp(0x40); // Set Sample Rate Constant
    sb16_write_dsp(256 - (1000000 / 8000));

    sb16_write_dsp(0x14); // Single-Cycle Output
    uint16_t transfer_count = dma_idx - 1;
    sb16_write_dsp(transfer_count & 0xFF);
    sb16_write_dsp((transfer_count >> 8) & 0xFF);

    // Minor hardware tick loop to let the channel latch open without blocking disk reads
    for (uint32_t loop = 0; loop < 4; loop++) {
        outb(0x43, 0x30);
        outb(0x40, 0xA9);
        outb(0x40, 0x04);
        while (1) {
            outb(0x43, 0x00);
            uint8_t low = inb(0x40);
            uint8_t high = inb(0x40);
            uint16_t counter = (high << 8) | low;
            if (counter > 0x04A9) break;
        }
    }

    return size;
}

static VFS_Operations sb16_vfs_ops = {
    .open = sb16_open,
    .close = (void*)0,
    .read = (void*)0,
    .write = sb16_write
};

void sb16_init() {
}

int vfs_mount_audio_device() {
    return vfs_mount("/dev/sound", &sb16_vfs_ops);
}
