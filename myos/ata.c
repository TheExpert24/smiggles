// ata.c: Basic ATA PIO driver for protected mode disk access
#include "kernel.h"
#include <stdint.h>
#include <stddef.h>

#define ATA_PRIMARY_IO  0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_MASTER      0xE0
#define ATA_SLAVE       0xF0

static void io_wait() {
    for (volatile int i = 0; i < 1000; i++); // crude delay
}

// Wait until the drive is not busy
static void ata_wait_busy() {
    while (inb(ATA_PRIMARY_IO + 7) & 0x80);
}

// Wait until the drive is ready for data
static void ata_wait_drq() {
    while (!(inb(ATA_PRIMARY_IO + 7) & 0x08));
}

// Read a sector (512 bytes) from LBA into buffer
int ata_read_sector(unsigned int lba, void* buffer) {
    if (buffer == NULL) return -2;
    uint8_t* buf = (uint8_t*)buffer;
    ata_wait_busy();
    outb(ATA_PRIMARY_CTRL, 0x00); // disable IRQs
    outb(ATA_PRIMARY_IO + 6, (lba >> 24) | ATA_MASTER);
    outb(ATA_PRIMARY_IO + 2, 1); // sector count
    outb(ATA_PRIMARY_IO + 3, lba & 0xFF);
    outb(ATA_PRIMARY_IO + 4, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_IO + 5, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_IO + 7, 0x20); // READ SECTORS
    ata_wait_busy();
    if (!(inb(ATA_PRIMARY_IO + 7) & 0x08)) return -1; // DRQ not set
    for (int i = 0; i < 256; i++) {
        uint16_t data = inw(ATA_PRIMARY_IO);
        buf[i*2] = data & 0xFF;
        buf[i*2+1] = (data >> 8) & 0xFF;
    }
    io_wait();
    return 0;
}

// Write a sector (512 bytes) from buffer to LBA
int ata_write_sector(unsigned int lba, const void* buffer) {
    if (buffer == NULL) return -2;
    const uint8_t* buf = (const uint8_t*)buffer;
    ata_wait_busy();
    outb(ATA_PRIMARY_CTRL, 0x00); // disable IRQs
    outb(ATA_PRIMARY_IO + 6, (lba >> 24) | ATA_MASTER);
    outb(ATA_PRIMARY_IO + 2, 1); // sector count
    outb(ATA_PRIMARY_IO + 3, lba & 0xFF);
    outb(ATA_PRIMARY_IO + 4, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_IO + 5, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_IO + 7, 0x30); // WRITE SECTORS
    ata_wait_busy();
    if (!(inb(ATA_PRIMARY_IO + 7) & 0x08)) return -1; // DRQ not set
    for (int i = 0; i < 256; i++) {
        uint16_t data = buf[i*2] | (buf[i*2+1] << 8);
        outw(ATA_PRIMARY_IO, data);
    }
    io_wait();
    return 0;
}
