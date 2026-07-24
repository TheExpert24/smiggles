#include "kernel.h"

// Explicitly pull in your speaker functions from your pci speaker logic
void play_sound(uint32_t frequency);
void nosound(void);

static int ac97_open(const char* path, int flags) {
    (void)path;
    (void)flags;
    return 0; 
}

static int ac97_write(int backend_fd, const char* buf, int size) {
    (void)backend_fd;
    if (size < 4) return size;

    // Read the decoded PCM wave bytes being streamed from the VFS
    int16_t left_sample = (int16_t)((uint8_t)buf[0] | ((uint8_t)buf[1] << 8));
    
    // Convert the amplitude data into an active speaker frequency value
    if (left_sample > 2000) {
        play_sound(440); 
    } else if (left_sample < -2000) {
        play_sound(880); 
    } else {
        nosound();
    }

    return size;
}

static VFS_Operations ac97_vfs_ops = {
    .open = ac97_open,
    .close = (void*)0,
    .read = (void*)0,
    .write = ac97_write
};

void ac97_init() {
    // Keep this empty so your existing kernel hooks remain unbroken
}

int vfs_mount_audio_device() {
    return vfs_mount("/dev/sound", &ac97_vfs_ops);
}
