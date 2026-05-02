GIF conversion tool

Use `convert_gif.py` to convert animated GIFs into the .bgf format consumed by the kernel `playgif` command.

Example:

    python tools/convert_gif.py badapple.gif badapple.bgf --width 80 --height 25 --delay 50

Then copy `badapple.bgf` into the filesystem used by the kernel (e.g., root of the image) and run in the shell:

    playgif /badapple.bgf

Requirements:

- Python 3
- Pillow (`pip install Pillow`)
