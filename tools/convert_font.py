
from pathlib import Path

INPUT = Path("VGA8.F16")
OUTPUT = Path("font_vga.h")

WIDTH = 8
HEIGHT = 16
CHARACTERS = 256

# Read the raw bitmap font
data = INPUT.read_bytes()

expected_size = CHARACTERS * HEIGHT

if len(data) != expected_size:
    raise ValueError(
        f"Expected {expected_size} bytes, got {len(data)}"
    )

# Generate C header
with OUTPUT.open("w") as f:
    f.write("#ifndef FONT_VGA_H\n")
    f.write("#define FONT_VGA_H\n\n")
    f.write("#include <stdint.h>\n\n")

    f.write("#define FONT_WIDTH 8\n")
    f.write("#define FONT_HEIGHT 16\n")
    f.write("#define FONT_CHARACTERS 256\n\n")

    f.write(
        "static const uint8_t font8x16"
        "[FONT_CHARACTERS][FONT_HEIGHT] = {\n"
    )

    for c in range(CHARACTERS):
        glyph = data[c * HEIGHT:(c + 1) * HEIGHT]

        f.write("    {")

        f.write(", ".join(
            f"0x{byte:02X}" for byte in glyph
        ))

        f.write("},\n")

    f.write("};\n\n")
    f.write("#endif\n")

print(f"Generated {OUTPUT}")
print(f"Converted {CHARACTERS} characters.")
