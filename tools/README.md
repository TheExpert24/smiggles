GIF conversion tool

Use `convert_gif.py` to convert animated GIFs into the .bgf format consumed by the kernel `playgif` command.

## Formats

- **BGF1** (default): Monochrome (1-bit, black & white only)
- **BGF4** (with `--color`): Full 16-color VGA palette

## Examples

Monochrome (original format):

    python tools/convert_gif.py badapple.gif badapple.bgf --width 80 --height 25 --delay 50

16-color (recommended for better visuals):

    python tools/convert_gif.py badapple.gif badapple.bgf --width 80 --height 25 --delay 50 --color

Then copy `badapple.bgf` into the filesystem and run:

    playgif /badapple.bgf

## Requirements

- Python 3
- Pillow (`pip install Pillow`)
