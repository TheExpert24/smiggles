#!/usr/bin/env python3
from __future__ import annotations
import os
import struct
import sys
from pathlib import Path

SECTOR_SIZE = 512
BLOCK_SIZE = 4096
SECTORS_PER_BLOCK = BLOCK_SIZE // SECTOR_SIZE
FS_MAGIC = 0x534D4947
FS_SUPERBLOCK_SECTOR = 2
FS_INODE_BITMAP_SECTOR = 3
FS_BLOCK_BITMAP_START_SECTOR = 4
FS_INODE_TABLE_START_SECTOR = 10
FS_DATA_START_SECTOR = 2010
FS_TOTAL_INODES = 4096
FS_TOTAL_BLOCKS = 2048
INODE_MODE_FILE = 0x0000
INODE_PERM_OWNER_R = 0x0100
INODE_PERM_OWNER_W = 0x0080
INODE_PERM_GROUP_R = 0x0020
INODE_PERM_OTHERS_R = 0x0004
DIRENT_TYPE_FILE = 1
DIRENT_SIZE = 260
SUPERBLOCK_FMT = "<13I"
DIRECTORY_ENTRY_FMT = "<I H B B 252s"

def read_sector(f, sector):
    f.seek(sector * SECTOR_SIZE)
    data = f.read(SECTOR_SIZE)
    if len(data) != SECTOR_SIZE: raise RuntimeError("short read")
    return bytearray(data)

def write_sector(f, sector, data):
    f.seek(sector * SECTOR_SIZE)
    f.write(data)

def read_block(f, block_num):
    f.seek((FS_DATA_START_SECTOR + block_num * SECTORS_PER_BLOCK) * SECTOR_SIZE)
    data = f.read(BLOCK_SIZE)
    if len(data) != BLOCK_SIZE: raise RuntimeError("short block read")
    return bytearray(data)

def write_block(f, block_num, data):
    f.seek((FS_DATA_START_SECTOR + block_num * SECTORS_PER_BLOCK) * SECTOR_SIZE)
    f.write(data)

def bit_get(buf, idx): return (buf[idx // 8] >> (idx % 8)) & 1
def bit_set(buf, idx): buf[idx // 8] |= 1 << (idx % 8)

def find_free_bit(buf, limit):
    for i in range(limit):
        if not bit_get(buf, i): return i
    return -1

def read_inode(f, inode_num):
    sector = FS_INODE_TABLE_START_SECTOR + inode_num // 2
    offset = (inode_num % 2) * 256
    sector_buf = read_sector(f, sector)
    return bytearray(sector_buf[offset:offset + 256])

def write_inode(f, inode_num, inode_bytes):
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
    if indirect_block == 0: return 0
    indirect = read_block(f, indirect_block)
    return struct.unpack_from("<I", indirect, file_block_idx * 4)[0]

def allocate_block(block_bitmap, counters):
    idx = find_free_bit(block_bitmap, FS_TOTAL_BLOCKS)
    if idx < 0: raise RuntimeError("no free blocks")
    bit_set(block_bitmap, idx)
    counters[0] -= 1
    return idx

def allocate_inode(inode_bitmap, counters):
    idx = find_free_bit(inode_bitmap, FS_TOTAL_INODES)
    if idx < 0: raise RuntimeError("no free inodes")
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
        if block_num == 0: break
        block = read_block(f, block_num)
        take = min(remaining, BLOCK_SIZE)
        out.extend(block[:take])
        remaining -= take
        block_idx += 1
    return bytes(out)

def write_file_bytes(f, inode_bytes, data, block_bitmap, counters):
    for i in range(12): 
        struct.pack_into("<I", inode_bytes, 40 + i * 4, 0)
    struct.pack_into("<I", inode_bytes, 88, 0)
    
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
            
            offset_slot = (block_idx - 12) * 4
            if offset_slot >= BLOCK_SIZE:
                raise RuntimeError("Audio file exceeds single-indirect filesystem boundaries")
                
            struct.pack_into("<I", indirect_entries, offset_slot, block_num)
            
    if indirect_block != 0:
        write_block(f, indirect_block, indirect_entries)
    struct.pack_into("<I", inode_bytes, 4, len(data))

def main(argv):
    if len(argv) < 2: return 1
    img_path = Path(argv[1])
    if not img_path.exists(): return 1
    wav_paths = sorted(Path(".").glob("*.wav"))
    if not wav_paths:
        print("No .wav files found to seed.")
        return 0
    with img_path.open("r+b") as f:
        sb = read_sector(f, FS_SUPERBLOCK_SECTOR)
        magic, _, _, _, _, _, free_blocks, free_inodes, *_ = struct.unpack_from(SUPERBLOCK_FMT, sb, 0)
        if magic != FS_MAGIC: return 1
        inode_bitmap = read_sector(f, FS_INODE_BITMAP_SECTOR)
        block_bitmap = bytearray()
        block_bitmap.extend(read_sector(f, FS_BLOCK_BITMAP_START_SECTOR))
        block_bitmap.extend(read_sector(f, FS_BLOCK_BITMAP_START_SECTOR + 1))
        counters = [free_blocks, free_inodes]
        root_inode_num = struct.unpack_from("<I", sb, 48)[0]
        root_inode = read_inode(f, root_inode_num)
        root_dir = read_dir_bytes(f, root_inode)
        existing_names = set()
        for off in range(0, len(root_dir), DIRENT_SIZE):
            chunk = root_dir[off:off + DIRENT_SIZE]
            if len(chunk) < DIRENT_SIZE: break
            _, _, name_len, _, name_bytes = struct.unpack(DIRECTORY_ENTRY_FMT, chunk)
            name = name_bytes[:name_len].decode("utf-8", errors="ignore")
            if name: existing_names.add(name)
        for wav_path in wav_paths:
            filename = wav_path.name
            if filename in existing_names: continue
            wav_bytes = wav_path.read_bytes()
            new_inode_num = allocate_inode(inode_bitmap, counters)
            new_inode = bytearray(256)
            struct.pack_into("<H", new_inode, 0, INODE_MODE_FILE | INODE_PERM_OWNER_R | INODE_PERM_OWNER_W | INODE_PERM_GROUP_R | INODE_PERM_OTHERS_R)
            struct.pack_into("<I", new_inode, 4, len(wav_bytes))
            struct.pack_into("<H", new_inode, 26, 1)
            write_file_bytes(f, new_inode, wav_bytes, block_bitmap, counters)
            write_inode(f, new_inode_num, new_inode)
            entry = struct.pack(DIRECTORY_ENTRY_FMT, new_inode_num, DIRENT_SIZE, len(filename), DIRENT_TYPE_FILE, filename.encode().ljust(252, b"\x00"))
            root_dir += entry
            existing_names.add(filename)
            print(f" seeded music {filename}")
        struct.pack_into("<I", root_inode, 4, len(root_dir))
        needed_blocks = (len(root_dir) + BLOCK_SIZE - 1) // BLOCK_SIZE
        for i in range(needed_blocks):
            chunk = root_dir[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]
            if len(chunk) < BLOCK_SIZE: chunk = chunk + bytes(BLOCK_SIZE - len(chunk))
            block_num = inode_get_block(f, root_inode, i)
            if block_num == 0: block_num = allocate_block(block_bitmap, counters)
            if i < 12: struct.pack_into("<I", root_inode, 40 + i * 4, block_num)
            write_block(f, block_num, chunk)
        write_inode(f, root_inode_num, root_inode)
        write_sector(f, FS_INODE_BITMAP_SECTOR, inode_bitmap)
        write_sector(f, FS_BLOCK_BITMAP_START_SECTOR, block_bitmap[:512])
        write_sector(f, FS_BLOCK_BITMAP_START_SECTOR + 1, block_bitmap[512:1024])
        struct.pack_into("<I", sb, 24, counters[0])
        struct.pack_into("<I", sb, 28, counters[1])
        write_sector(f, FS_SUPERBLOCK_SECTOR, sb)
    return 0

if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
