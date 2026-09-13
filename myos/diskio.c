#include "kernel.h"

int disk_read_sector(unsigned int lba, void* buffer) {
    return ata_read_sector(lba, buffer);
}
int disk_write_sector(unsigned int lba, const void* buffer) {
    return ata_write_sector(lba, buffer);
}
