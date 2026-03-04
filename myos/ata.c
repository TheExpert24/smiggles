// ata.c: Basic ATA PIO driver for protected mode disk access
#include "kernel.h"
#include <stdint.h>
#include <stddef.h>

#define ATA_PRIMARY_IO  0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_MASTER      0xE0
#define ATA_SLAVE       0xF0

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_BSY  0x80

#define ATA_POLL_TIMEOUT 1000000

static void io_wait() {
    for (volatile int i = 0; i < 1000; i++); // crude delay
}

// Wait until BSY clears; return 0 on success, negative on timeout/error.
static int ata_wait_busy(void) {
    for (unsigned int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        unsigned char status = inb(ATA_PRIMARY_IO + 7);
        if (status & ATA_STATUS_ERR) return -2;
        if (status & ATA_STATUS_DF) return -3;
        if (!(status & ATA_STATUS_BSY)) return 0;
    }
    return -1;
}

// Wait until DRQ is set and BSY clears; return 0 on success, negative on timeout/error.
static int ata_wait_drq(void) {
    for (unsigned int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        unsigned char status = inb(ATA_PRIMARY_IO + 7);
        if (status & ATA_STATUS_ERR) return -2;
        if (status & ATA_STATUS_DF) return -3;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) return 0;
    }
    return -1;
}

// Read a sector (512 bytes) from LBA into buffer
int ata_read_sector(unsigned int lba, void* buffer) {
    if (buffer == NULL) return -2;
    uint8_t* buf = (uint8_t*)buffer;
    if (ata_wait_busy() != 0) return -1;
    outb(ATA_PRIMARY_CTRL, 0x00); // disable IRQs
    outb(ATA_PRIMARY_IO + 6, (lba >> 24) | ATA_MASTER);
    outb(ATA_PRIMARY_IO + 2, 1); // sector count
    outb(ATA_PRIMARY_IO + 3, lba & 0xFF);
    outb(ATA_PRIMARY_IO + 4, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_IO + 5, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_IO + 7, 0x20); // READ SECTORS
    if (ata_wait_busy() != 0) return -1;
    if (ata_wait_drq() != 0) return -1;
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
    if (ata_wait_busy() != 0) return -1;
    outb(ATA_PRIMARY_CTRL, 0x00); // disable IRQs
    outb(ATA_PRIMARY_IO + 6, (lba >> 24) | ATA_MASTER);
    outb(ATA_PRIMARY_IO + 2, 1); // sector count
    outb(ATA_PRIMARY_IO + 3, lba & 0xFF);
    outb(ATA_PRIMARY_IO + 4, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_IO + 5, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_IO + 7, 0x30); // WRITE SECTORS
    if (ata_wait_busy() != 0) return -1;
    if (ata_wait_drq() != 0) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t data = buf[i*2] | (buf[i*2+1] << 8);
        outw(ATA_PRIMARY_IO, data);
    }

    outb(ATA_PRIMARY_IO + 7, 0xE7); // FLUSH CACHE
    if (ata_wait_busy() != 0) return -1;

    io_wait();
    return 0;
}
