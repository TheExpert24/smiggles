#!/usr/bin/env python3
"""Seed badapple.bgf into an existing Smiggles hdd.img newfs image.

This does not touch the kernel or boot path. It expects hdd.img to already
contain a formatted Smiggles filesystem.
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

SECTOR_SIZE = 512
BLOCK_SIZE = 4096
SECTORS_PER_BLOCK = BLOCK_SIZE // SECTOR_SIZE

FS_MAGIC = 0x534D4947
FS_BLOCK_SIZE = 4096
FS_SUPERBLOCK_SECTOR = 2
FS_INODE_BITMAP_SECTOR = 3
FS_BLOCK_BITMAP_START_SECTOR = 4
FS_INODE_TABLE_START_SECTOR = 10
FS_DATA_START_SECTOR = 2010
FS_TOTAL_INODES = 4096
FS_TOTAL_BLOCKS = 2048

INODE_MODE_DIR = 0x8000
INODE_MODE_FILE = 0x0000
INODE_PERM_OWNER_R = 0x0100
INODE_PERM_OWNER_W = 0x0080
INODE_PERM_OWNER_X = 0x0040
INODE_PERM_GROUP_R = 0x0020
INODE_PERM_GROUP_W = 0x0010
INODE_PERM_GROUP_X = 0x0008
INODE_PERM_OTHERS_R = 0x0004
INODE_PERM_OTHERS_W = 0x0002
INODE_PERM_OTHERS_X = 0x0001

DIRENT_TYPE_FILE = 1
DIRENT_NAME_MAX = 251
DIRENT_SIZE = 260

SUPERBLOCK_FMT = "<13I"  # Added root_inode_num field
INODE_FMT = "<HHIIIIHHIII12IIIIIIII"
DIRECTORY_ENTRY_FMT = "<I H B B 252s"


def read_sector(f, sector):
    f.seek(sector * SECTOR_SIZE)
    data = f.read(SECTOR_SIZE)
    if len(data) != SECTOR_SIZE:
        raise RuntimeError(f"short read at sector {sector}")
    return bytearray(data)


def write_sector(f, sector, data):
    if len(data) != SECTOR_SIZE:
        raise RuntimeError(f"sector write must be exactly 512 bytes, got {len(data)}")
    f.seek(sector * SECTOR_SIZE)
    f.write(data)


def read_block(f, block_num):
    f.seek((FS_DATA_START_SECTOR + block_num * SECTORS_PER_BLOCK) * SECTOR_SIZE)
    data = f.read(BLOCK_SIZE)
    if len(data) != BLOCK_SIZE:
        raise RuntimeError(f"short read at block {block_num}")
    return bytearray(data)


def write_block(f, block_num, data):
    if len(data) != BLOCK_SIZE:
        raise RuntimeError(f"block write must be exactly 4096 bytes, got {len(data)}")
    f.seek((FS_DATA_START_SECTOR + block_num * SECTORS_PER_BLOCK) * SECTOR_SIZE)
    f.write(data)


def bit_get(buf, idx):
    return (buf[idx // 8] >> (idx % 8)) & 1


def bit_set(buf, idx):
    buf[idx // 8] |= 1 << (idx % 8)


def bit_clear(buf, idx):
    buf[idx // 8] &= ~(1 << (idx % 8)) & 0xFF


def find_free_bit(buf, limit):
    for i in range(limit):
        if not bit_get(buf, i):
            return i
    return -1


def read_inode(f, inode_num):
    sector = FS_INODE_TABLE_START_SECTOR + inode_num // 2
    offset = (inode_num % 2) * 256
    sector_buf = read_sector(f, sector)
    return bytearray(sector_buf[offset:offset + 256])


def write_inode(f, inode_num, inode_bytes):
    if len(inode_bytes) != 256:
        raise RuntimeError("inode must be 256 bytes")
    sector = FS_INODE_TABLE_START_SECTOR + inode_num // 2
    offset = (inode_num % 2) * 256
    sector_buf = read_sector(f, sector)
    sector_buf[offset:offset + 256] = inode_bytes
    write_sector(f, sector, sector_buf)


def inode_get_block(f, inode_bytes, file_block_idx):
    if file_block_idx < 12:
        return struct.unpack_from("<I", inode_bytes, 40 + file_block_idx * 4)[0]

    file_block_idx -= 12
    indirect_block = struct.unpack_from("<I", inode_bytes, 88)[0]
    if indirect_block == 0:
        return 0
    indirect = read_block(f, indirect_block)
    return struct.unpack_from("<I", indirect, file_block_idx * 4)[0]


def inode_set_block(f, inode_bytes, file_block_idx, block_num, block_bitmap, counters):
    if file_block_idx < 12:
        struct.pack_into("<I", inode_bytes, 40 + file_block_idx * 4, block_num)
        return

    file_block_idx -= 12
    indirect_block = struct.unpack_from("<I", inode_bytes, 88)[0]
    if indirect_block == 0:
        indirect_block = allocate_block(block_bitmap, counters)
        struct.pack_into("<I", inode_bytes, 88, indirect_block)
        zero = bytearray(BLOCK_SIZE)
        write_block(f, indirect_block, zero)
    indirect = read_block(f, indirect_block)
    struct.pack_into("<I", indirect, file_block_idx * 4, block_num)
    write_block(f, indirect_block, indirect)


def allocate_block(block_bitmap, counters):
    idx = find_free_bit(block_bitmap, FS_TOTAL_BLOCKS)
    if idx < 0:
        raise RuntimeError("no free data blocks")
    bit_set(block_bitmap, idx)
    counters[0] -= 1
    return idx


def allocate_inode(inode_bitmap, counters):
    idx = find_free_bit(inode_bitmap, FS_TOTAL_INODES)
    if idx < 0:
        raise RuntimeError("no free inodes")
    bit_set(inode_bitmap, idx)
    counters[1] -= 1
    return idx


def read_dir_bytes(f, inode_bytes):
    size = struct.unpack_from("<I", inode_bytes, 4)[0]
    out = bytearray()
    remaining = size
    block_idx = 0
    while remaining > 0:
        block_num = inode_get_block(f, inode_bytes, block_idx)
        if block_num == 0:
            break
        block = read_block(f, block_num)
        take = min(remaining, BLOCK_SIZE)
        out.extend(block[:take])
        remaining -= take
        block_idx += 1
    return bytes(out)


def write_file_bytes(f, inode_bytes, data, block_bitmap, counters):
    # Clear existing block pointers for a fresh write.
    for i in range(12):
        struct.pack_into("<I", inode_bytes, 40 + i * 4, 0)
    struct.pack_into("<I", inode_bytes, 88, 0)
    struct.pack_into("<I", inode_bytes, 92, 0)
    struct.pack_into("<I", inode_bytes, 96, 0)

    total_blocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
    if total_blocks == 0:
        struct.pack_into("<I", inode_bytes, 4, 0)
        return

    indirect_entries = bytearray(BLOCK_SIZE)
    indirect_block = 0
    for block_idx in range(total_blocks):
        block_num = allocate_block(block_bitmap, counters)
        start = block_idx * BLOCK_SIZE
        chunk = data[start:start + BLOCK_SIZE]
        if len(chunk) < BLOCK_SIZE:
            chunk = chunk + bytes(BLOCK_SIZE - len(chunk))
        write_block(f, block_num, chunk)
        if block_idx < 12:
            struct.pack_into("<I", inode_bytes, 40 + block_idx * 4, block_num)
        else:
            if indirect_block == 0:
                indirect_block = allocate_block(block_bitmap, counters)
                struct.pack_into("<I", inode_bytes, 88, indirect_block)
            struct.pack_into("<I", indirect_entries, (block_idx - 12) * 4, block_num)

    if indirect_block != 0:
        write_block(f, indirect_block, indirect_entries)

    struct.pack_into("<I", inode_bytes, 4, len(data))


def format_filesystem(f):
    """Format hdd.img with an empty Smiggles filesystem."""
    # Write superblock
    sb = bytearray(SECTOR_SIZE)
    struct.pack_into(SUPERBLOCK_FMT, sb, 0,
        FS_MAGIC, 1, FS_BLOCK_SIZE, 256,
        FS_TOTAL_BLOCKS, FS_TOTAL_INODES,
        FS_TOTAL_BLOCKS - 1, FS_TOTAL_INODES - 2,
        0, 0, 0, 0, 1)  # Added root_inode_num = 1 as the 13th field
    write_sector(f, FS_SUPERBLOCK_SECTOR, sb)
    
    # Initialize inode bitmap (inode 0 reserved, inode 1 is root)
    inode_bitmap = bytearray(SECTOR_SIZE)
    bit_set(inode_bitmap, 0)
    bit_set(inode_bitmap, 1)
    write_sector(f, FS_INODE_BITMAP_SECTOR, inode_bitmap)
    
    # Initialize block bitmap (block 0 is reserved)
    block_bitmap = bytearray(SECTOR_SIZE * 2)
    bit_set(block_bitmap, 0)
    write_sector(f, FS_BLOCK_BITMAP_START_SECTOR, block_bitmap[:512])
    write_sector(f, FS_BLOCK_BITMAP_START_SECTOR + 1, block_bitmap[512:1024])
    
    # Create root inode
    root_inode = bytearray(256)
    struct.pack_into("<H", root_inode, 0, INODE_MODE_DIR | 0o755)
    struct.pack_into("<I", root_inode, 4, 0)
    struct.pack_into("<H", root_inode, 24, 0)
    struct.pack_into("<H", root_inode, 26, 1)
    write_inode(f, 1, root_inode)


def main(argv):
    if len(argv) != 3:
        print(f"usage: {Path(argv[0]).name} <hdd.img> <badapple.bgf>")
        return 2

    img_path = Path(argv[1])
    bgf_path = Path(argv[2])
    if not img_path.exists():
        print(f"error: missing image: {img_path}")
        return 1
    if not bgf_path.exists():
        print(f"error: missing bgf: {bgf_path}")
        return 1

    bgf = bgf_path.read_bytes()

    with img_path.open("r+b") as f:
        sb = read_sector(f, FS_SUPERBLOCK_SECTOR)
        magic, version, block_size, inode_size, total_blocks, total_inodes, free_blocks, free_inodes, *_ = struct.unpack_from(SUPERBLOCK_FMT, sb, 0)
        if magic != FS_MAGIC:
            print("hdd.img not formatted; formatting...")
            format_filesystem(f)
            sb = read_sector(f, FS_SUPERBLOCK_SECTOR)
            magic, version, block_size, inode_size, total_blocks, total_inodes, free_blocks, free_inodes, *_ = struct.unpack_from(SUPERBLOCK_FMT, sb, 0)
        if block_size != FS_BLOCK_SIZE:
            print(f"unexpected block size: {block_size}")
            return 1

        inode_bitmap = read_sector(f, FS_INODE_BITMAP_SECTOR)
        block_bitmap = bytearray()
        block_bitmap.extend(read_sector(f, FS_BLOCK_BITMAP_START_SECTOR))
        block_bitmap.extend(read_sector(f, FS_BLOCK_BITMAP_START_SECTOR + 1))
        counters = [free_blocks, free_inodes]

        root_inode_num = struct.unpack_from("<I", sb, 48)[0]
        root_inode = read_inode(f, root_inode_num)

        root_dir = read_dir_bytes(f, root_inode)
        entries = []
        existing_names = set()
        for off in range(0, len(root_dir), DIRENT_SIZE):
            chunk = root_dir[off:off + DIRENT_SIZE]
            if len(chunk) < DIRENT_SIZE:
                break
            inode_num, rec_len, name_len, file_type, name_bytes = struct.unpack(DIRECTORY_ENTRY_FMT, chunk)
            name = name_bytes[:name_len].decode("utf-8", errors="ignore")
            if name:
                existing_names.add(name)
            entries.append((inode_num, rec_len, name_len, file_type, name_bytes))

        if "badapple.bgf" in existing_names:
            print("badapple.bgf already present in hdd.img")
            return 0

        # Allocate inode for the file.
        new_inode_num = allocate_inode(inode_bitmap, counters)
        new_inode = bytearray(256)
        struct.pack_into("<H", new_inode, 0, INODE_MODE_FILE | INODE_PERM_OWNER_R | INODE_PERM_OWNER_W | INODE_PERM_GROUP_R | INODE_PERM_OTHERS_R)
        struct.pack_into("<H", new_inode, 2, 0)
        struct.pack_into("<I", new_inode, 4, len(bgf))
        struct.pack_into("<H", new_inode, 24, 0)
        struct.pack_into("<H", new_inode, 26, 1)

        write_file_bytes(f, new_inode, bgf, block_bitmap, counters)
        write_inode(f, new_inode_num, new_inode)

        # Append entry to root directory.
        name = b"badapple.bgf"
        entry = struct.pack(DIRECTORY_ENTRY_FMT, new_inode_num, DIRENT_SIZE, len(name), DIRENT_TYPE_FILE, name.ljust(252, b"\x00"))
        root_dir += entry
        struct.pack_into("<I", root_inode, 4, len(root_dir))

        # Reuse existing blocks for the root directory if possible.
        needed_blocks = (len(root_dir) + BLOCK_SIZE - 1) // BLOCK_SIZE
        for i in range(needed_blocks):
            start = i * BLOCK_SIZE
            chunk = root_dir[start:start + BLOCK_SIZE]
            if len(chunk) < BLOCK_SIZE:
                chunk = chunk + bytes(BLOCK_SIZE - len(chunk))
            block_num = inode_get_block(f, root_inode, i)
            if block_num == 0:
                block_num = allocate_block(block_bitmap, counters)
                if i < 12:
                    struct.pack_into("<I", root_inode, 40 + i * 4, block_num)
                else:
                    # Root directory should never be this large in practice.
                    raise RuntimeError("root directory unexpectedly large")
            else:
                pass
            write_block(f, block_num, chunk)

        write_inode(f, root_inode_num, root_inode)

        # Write bitmaps and updated superblock.
        write_sector(f, FS_INODE_BITMAP_SECTOR, inode_bitmap)
        write_sector(f, FS_BLOCK_BITMAP_START_SECTOR, block_bitmap[:512])
        write_sector(f, FS_BLOCK_BITMAP_START_SECTOR + 1, block_bitmap[512:1024])
        struct.pack_into("<I", sb, 24, counters[0])
        struct.pack_into("<I", sb, 28, counters[1])
        write_sector(f, FS_SUPERBLOCK_SECTOR, sb)

    print(f"seeded {bgf_path.name} into {img_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
