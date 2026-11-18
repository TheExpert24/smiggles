#include "kernel.h"
#include <stdint.h>

#define RAMDISK_SECTOR_SIZE 512u
#define RAMDISK_SECTOR_COUNT 20480u

static uint8_t ramdisk[RAMDISK_SECTOR_SIZE * RAMDISK_SECTOR_COUNT];
static int ramdisk_active = 0;

static int ramdisk_valid(unsigned int lba, const void* buffer) {
    return buffer != 0 && lba < RAMDISK_SECTOR_COUNT;
}

static int ramdisk_read(unsigned int lba, void* buffer) {
    uint8_t* destination = (uint8_t*)buffer;
    const uint8_t* source = &ramdisk[lba * RAMDISK_SECTOR_SIZE];
    for (unsigned int i = 0; i < RAMDISK_SECTOR_SIZE; i++) destination[i] = source[i];
    return 0;
}

static int ramdisk_write(unsigned int lba, const void* buffer) {
    uint8_t* destination = &ramdisk[lba * RAMDISK_SECTOR_SIZE];
    const uint8_t* source = (const uint8_t*)buffer;
    for (unsigned int i = 0; i < RAMDISK_SECTOR_SIZE; i++) destination[i] = source[i];
    return 0;
}