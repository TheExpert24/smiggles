#!/usr/bin/env python3
"""
Convert an animated GIF to a compact frame bundle (.bgf).

Usage: python tools/convert_gif.py input.gif output.bgf [--width 80] [--height 25] [--delay 50] [--color]

Produces binary format:
  BGF1 (monochrome, default):
    0-3  : 'BGF1' magic
    4-13 : header (width, height, frames, delay)
    Data : 1 bit per pixel, packed MSB-first in bytes
  
  BGF4 (16-color, if --color flag):
    0-3  : 'BGF4' magic
    4-13 : header (width, height, frames, delay)
    Data : 4 bits per pixel, 2 pixels per byte (VGA palette 0-15)

Requires: Pillow
"""
import sys
from PIL import Image, ImageSequence
import struct

# Standard VGA 16-color palette (RGB values)
VGA_PALETTE = [
    (0,   0,   0),      # 0x00: Black
    (0,   0,   170),    # 0x01: Blue
    (0,   170, 0),      # 0x02: Green
    (0,   170, 170),    # 0x03: Cyan
    (170, 0,   0),      # 0x04: Red
    (170, 0,   170),    # 0x05: Magenta
    (170, 85,  0),      # 0x06: Brown
    (170, 170, 170),    # 0x07: Light Gray
    (85,  85,  85),     # 0x08: Dark Gray
    (85,  85,  255),    # 0x09: Light Blue
    (85,  255, 85),     # 0x0A: Light Green
    (85,  255, 255),    # 0x0B: Light Cyan
    (255, 85,  85),     # 0x0C: Light Red
    (255, 85,  255),    # 0x0D: Light Magenta
    (255, 255, 85),     # 0x0E: Yellow
    (255, 255, 255),    # 0x0F: White
]

def usage():
    print("Usage: convert_gif.py input.gif output.bgf [--width W] [--height H] [--delay MS] [--color]")

def main(argv):
    if len(argv) < 3:
        usage(); return 1
    infile = argv[1]
    outfile = argv[2]
    width = 80
    height = 25
    delay = 50
    use_color = False
    i = 3
    while i < len(argv):
        if argv[i] == '--width' and i+1 < len(argv): width = int(argv[i+1]); i += 2
        elif argv[i] == '--height' and i+1 < len(argv): height = int(argv[i+1]); i += 2
        elif argv[i] == '--delay' and i+1 < len(argv): delay = int(argv[i+1]); i += 2
        elif argv[i] == '--color': use_color = True; i += 1
        else:
            i += 1

    im = Image.open(infile)
    frames = []
    
    if use_color:
        # Quantize to 16 colors using VGA palette
        for frame in ImageSequence.Iterator(im):
            f = frame.convert('RGB').resize((width, height), Image.LANCZOS)
            # Create a custom palette image
            pal_img = Image.new('P', (1, 1))
            pal_data = []
            for r, g, b in VGA_PALETTE:
                pal_data.extend([r, g, b])
            pal_img.putpalette(pal_data)
            # Quantize to the palette
            quantized = f.quantize(palette=pal_img)
            frames.append(quantized)
        
        frame_count = len(frames)
        frame_bytes = ((width * height) + 1) // 2  # 4 bits per pixel, 2 pixels per byte
        
        with open(outfile, 'wb') as f:
            f.write(b'BGF4')
            f.write(struct.pack('<H', width))
            f.write(struct.pack('<H', height))
            f.write(struct.pack('<I', frame_count))
            f.write(struct.pack('<H', delay))
            
            for col_img in frames:
                curbyte = 0
                nibble_pos = 1  # High nibble first
                for y in range(height):
                    for x in range(width):
                        color = col_img.getpixel((x, y)) & 0x0F
                        if nibble_pos == 1:
                            curbyte = (color & 0x0F) << 4
                            nibble_pos = 0
                        else:
                            curbyte |= (color & 0x0F)
                            f.write(bytes([curbyte]))
                            curbyte = 0
                            nibble_pos = 1
                if nibble_pos == 0:
                    f.write(bytes([curbyte]))
        
        print(f'Wrote {outfile} (BGF4): {width}x{height} frames={frame_count} delay={delay}ms [16-COLOR]')
    else:
        # Monochrome BGF1 format (original)
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
        
        print(f'Wrote {outfile} (BGF1): {width}x{height} frames={frame_count} delay={delay}ms [MONOCHROME]')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
