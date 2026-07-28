#include "kernel.h"

#define SB_DSP_RESET  0x226
#define SB_DSP_READ   0x22A
#define SB_DSP_WRITE  0x22C
#define SB_DSP_STATUS 0x22C

static __attribute__((aligned(4096))) uint8_t dma_buffer[16384];
static uint8_t ring_buffer[65536];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;
static int initial_skip_count = 0;
static int playback_active = 0;
static uint32_t current_sample_rate = 8000;

static void sb16_write_dsp(uint8_t val) {
    while (inb(SB_DSP_STATUS) & 0x80);
    outb(SB_DSP_WRITE, val);
}

static void isa_dma_setup_channel5(uint32_t buffer_phys, uint32_t length) {
    uint32_t words = length / 2;
    uint32_t count = words - 1;
    outb(0xD4, 0x04 | 1);
    outb(0xD8, 0x00);
    outb(0xD6, 0x44 | 0x10 | 0x00 | 1);
    outb(0xC4, (buffer_phys >> 1) & 0xFF);
    outb(0xC4, ((buffer_phys >> 1) >> 8) & 0xFF);
    outb(0x8B, (buffer_phys >> 16) & 0xFF);
    outb(0xC6, count & 0xFF);
    outb(0xC6, (count >> 8) & 0xFF);
    outb(0xD4, 0x00 | 1);
}

static int sb16_open(const char* path, int flags) {
    (void)path;
    (void)flags;
    outb(SB_DSP_RESET, 1);
    for (volatile int i = 0; i < 2000; i++);
    outb(SB_DSP_RESET, 0);
    volatile int timeout = 10000;
    while ((inb(0x22E) & 0x80) == 0 && timeout > 0) {
        timeout--;
    }
    if (inb(SB_DSP_READ) != 0xAA) return -1;
    sb16_write_dsp(0xD1);
    initial_skip_count = 44;
    
    // CRITICAL FIX: Reset all state flags cleanly on every file open
    ring_head = 0;
    ring_tail = 0;
    playback_active = 0;
    current_sample_rate = 8000;
    
    inb(0x22F); // Flush any stale hardware bits
    return 0;
}

static void sb16_pump_hardware(void) {
    static int current_buffer_half = 0;
    uint32_t available = (ring_head >= ring_tail) ? (ring_head - ring_tail) : (65536 - ring_tail + ring_head);
    if (available < 8192) {
        return;
    }
    int target_offset = (current_buffer_half == 0) ? 0 : 8192;
    for (int i = 0; i < 8192; i++) {
        dma_buffer[target_offset + i] = ring_buffer[ring_tail];
        ring_tail = (ring_tail + 1) % 65536;
    }
    inb(0x22F);

    uint32_t active_phys_addr = (uint32_t)dma_buffer + target_offset;
    isa_dma_setup_channel5(active_phys_addr, 8192);
    
    sb16_write_dsp(0x41);
    sb16_write_dsp((current_sample_rate >> 8) & 0xFF);
    sb16_write_dsp(current_sample_rate & 0xFF);
    
    sb16_write_dsp(0xB0);
    sb16_write_dsp(0x10);
    sb16_write_dsp((4096 - 1) & 0xFF);
    sb16_write_dsp(((4096 - 1) >> 8) & 0xFF);
    
    uint32_t micro_to_wait = (4096 * 1000000) / current_sample_rate;
    uint32_t loops_to_wait = (micro_to_wait * 16) / 4;
    for (volatile uint32_t k = 0; k < loops_to_wait; k++) {
        __asm__ volatile("pause");
    }

    current_buffer_half = (current_buffer_half == 0) ? 1 : 0;
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
    int incoming_bytes = size - start_offset;
    const char* src_ptr = buf + start_offset;
    for (int i = 0; i < incoming_bytes; i++) {
        uint32_t next_head = (ring_head + 1) % 65536;
        while (next_head == ring_tail) {
            sb16_pump_hardware();
        }
        ring_buffer[ring_head] = (uint8_t)src_ptr[i];
        ring_head = next_head;
    }
    if (!playback_active) {
        uint32_t loaded = (ring_head >= ring_tail) ? (ring_head - ring_tail) : (65536 - ring_tail + ring_head);
        if (loaded >= 16384) {
            playback_active = 1;
            while (playback_active) {
                uint32_t remaining = (ring_head >= ring_tail) ? (ring_head - ring_tail) : (65536 - ring_tail + ring_head);
                if (remaining < 8192) {
                    break;
                }
                sb16_pump_hardware();
            }
        }
    }
    return size;
}

void sb16_set_sample_rate(uint32_t rate) {
    if (rate > 0) current_sample_rate = rate;
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
