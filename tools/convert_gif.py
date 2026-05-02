#!/usr/bin/env python3
"""
Convert an animated GIF to a compact monochrome frame bundle (.bgf).

Usage: python tools/convert_gif.py input.gif output.bgf [--width 80] [--height 25] [--delay 50]

Produces a binary format:
  0-3  : 'BGF1' magic
  4-5  : u16 width (little-endian)
  6-7  : u16 height
  8-11 : u32 frames
 12-13 : u16 delay_ms per frame
 Frame data: each frame packed bits row-major, MSB-first in each byte

Requires: Pillow
"""
import sys
from PIL import Image, ImageSequence
import struct

def usage():
    print("Usage: convert_gif.py input.gif output.bgf [--width W] [--height H] [--delay MS]")

def main(argv):
    if len(argv) < 3:
        usage(); return 1
    infile = argv[1]
    outfile = argv[2]
    width = 80
    height = 25
    delay = 50
    i = 3
    while i < len(argv):
        if argv[i] == '--width' and i+1 < len(argv): width = int(argv[i+1]); i += 2
        elif argv[i] == '--height' and i+1 < len(argv): height = int(argv[i+1]); i += 2
        elif argv[i] == '--delay' and i+1 < len(argv): delay = int(argv[i+1]); i += 2
        else:
            i += 1

    im = Image.open(infile)
    frames = []
    for frame in ImageSequence.Iterator(im):
        f = frame.convert('L').resize((width, height), Image.LANCZOS)
        # threshold to 1-bit
        bw = f.point(lambda p: 255 if p > 128 else 0, mode='1')
        frames.append(bw)

    frame_count = len(frames)
    frame_bytes = ((width * height) + 7) // 8

    with open(outfile, 'wb') as f:
        f.write(b'BGF1')
        f.write(struct.pack('<H', width))
        f.write(struct.pack('<H', height))
        f.write(struct.pack('<I', frame_count))
        f.write(struct.pack('<H', delay))

        for bw in frames:
            bits = 0
            bitpos = 7
            curbyte = 0
            for y in range(height):
                for x in range(width):
                    v = 1 if bw.getpixel((x,y)) != 0 else 0
                    if v:
                        curbyte |= (1 << bitpos)
                    bitpos -= 1
                    if bitpos < 0:
                        f.write(bytes([curbyte]))
                        curbyte = 0
                        bitpos = 7
            if bitpos != 7:
                f.write(bytes([curbyte]))

    print(f'Wrote {outfile}: {width}x{height} frames={frame_count} delay={delay}ms')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
