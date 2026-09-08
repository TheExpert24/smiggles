#include "kernel.h"

// --- Display Functions ---

#define SCREEN_COLS 80
#define SCREEN_ROWS 25
#define SCREEN_CELLS (SCREEN_COLS * SCREEN_ROWS)
#define CELL_BYTES 2
#define SCREEN_BYTES (SCREEN_CELLS * CELL_BYTES)
#define SCROLLBACK_LINES 1024

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u
#define MB2_TAG_TYPE_FRAMEBUFFER 8u

struct mb2_tag_header {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_info_header {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_framebuffer_tag {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
    uint8_t color_info[6];
} __attribute__((packed));

static uint8_t* framebuffer = 0;
static uint32_t framebuffer_pitch = 0;
static uint32_t framebuffer_width = 0;
static uint32_t framebuffer_height = 0;
static uint8_t framebuffer_bpp = 0;
static uint8_t framebuffer_type = 0;
static uint8_t red_position = 0;
static uint8_t red_mask = 0;
static uint8_t green_position = 0;
static uint8_t green_mask = 0;
static uint8_t blue_position = 0;
static uint8_t blue_mask = 0;
static char text_shadow[SCREEN_BYTES];

static const uint8_t font8x8_basic[128][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0000 (nul)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0001
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0002
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0003
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0004
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0005
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0006
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0007
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0008
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0009
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0010
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0011
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0012
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0013
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0014
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0015
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0016
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0017
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0018
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0019
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (;)
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
    { 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
    { 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},   // U+0065 (e)
    { 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}    // U+007F
};

static uint32_t framebuffer_color(unsigned char color) {
    static const uint32_t colors[16] = {
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
    };
    return colors[color & 0x0F];
}

static void framebuffer_pixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!framebuffer || x >= framebuffer_width || y >= framebuffer_height) return;
    uint8_t* pixel = framebuffer + y * framebuffer_pitch + x * (framebuffer_bpp / 8);
    uint32_t red = (rgb >> 16) & 0xFF;
    uint32_t green = (rgb >> 8) & 0xFF;
    uint32_t blue = rgb & 0xFF;
    uint32_t packed = ((red >> (8 - red_mask)) << red_position) |
                      ((green >> (8 - green_mask)) << green_position) |
                      ((blue >> (8 - blue_mask)) << blue_position);
    if (framebuffer_bpp == 32) {
        *(uint32_t*)pixel = packed;
    } else if (framebuffer_bpp == 24) {
        pixel[0] = (uint8_t)(packed & 0xFF);
        pixel[1] = (uint8_t)((packed >> 8) & 0xFF);
        pixel[2] = (uint8_t)((packed >> 16) & 0xFF);
    }
}

static void framebuffer_draw_char(uint32_t col, uint32_t row,
                                  char ch, unsigned char color) {
    uint32_t fg = framebuffer_color(color);
    uint32_t bg = framebuffer_color(COLOR_BLACK);

    uint32_t x0 = col * 12;
    uint32_t y0 = row * 22;

    uint8_t c = (uint8_t)ch;

    // nly basic ASCII is supported by this font.
    if (c >= 128)
        c = '?';

    /*
     * font8x8_basic:
     *   8 pixels wide
     *   8 pixels tall
     *
     * Render each font pixel as 1x2 pixels.
     * This gives an 8x16 character inside
     * the existing 12x22 character cell.
     */

    for (uint32_t y = 0; y < 22; y++) {
        for (uint32_t x = 0; x < 12; x++) {

            int glyph_x = (int)x - 2;
            int glyph_y = ((int)y - 3) / 2;

            int set = 0;

            if (glyph_x >= 0 && glyph_x < 8 &&
                glyph_y >= 0 && glyph_y < 8) {

                set = (font8x8_basic[c][glyph_y] &
                       (1u << glyph_x)) != 0;
            }

            framebuffer_pixel(
                x0 + x,
                y0 + y,
                set ? fg : bg
            );
        }
    }
}

void display_set_multiboot_framebuffer(unsigned int mb_magic, unsigned int mb_info_addr) {
    framebuffer = 0;
    if (mb_magic != MULTIBOOT2_BOOTLOADER_MAGIC || !mb_info_addr) return;
    struct mb2_info_header* info = (struct mb2_info_header*)(uintptr_t)mb_info_addr;
    uint8_t* cursor = (uint8_t*)info + 8;
    uint8_t* end = (uint8_t*)info + info->total_size;
    int force_framebuffer = 0;
    int force_vga = 0;
    while (cursor + sizeof(struct mb2_tag_header) <= end) {
        struct mb2_tag_header* header = (struct mb2_tag_header*)cursor;
        if (header->type == 0) break;
        if (header->type == 1 && header->size > 8) {
            const char* command_line = (const char*)(cursor + 8);
            if (command_line[0] == 'v' && command_line[1] == 'g' && command_line[2] == 'a') force_vga = 1;
            if (command_line[0] == 'f' && command_line[1] == 'r' && command_line[2] == 'a') force_framebuffer = 1;
        }
        if (header->size < 8) break;
        cursor += (header->size + 7u) & ~7u;
    }
    if (force_vga) return;
    cursor = (uint8_t*)info + 8;
    while (cursor + sizeof(struct mb2_tag_header) <= end) {
        struct mb2_tag_header* header = (struct mb2_tag_header*)cursor;
        if (header->type == 0) break;
        if (header->type == MB2_TAG_TYPE_FRAMEBUFFER && header->size >= sizeof(struct mb2_framebuffer_tag)) {
            struct mb2_framebuffer_tag* tag = (struct mb2_framebuffer_tag*)cursor;
            framebuffer = (uint8_t*)(uintptr_t)tag->framebuffer_addr;
            framebuffer_pitch = tag->framebuffer_pitch;
            framebuffer_width = tag->framebuffer_width;
            framebuffer_height = tag->framebuffer_height;
            framebuffer_bpp = tag->framebuffer_bpp;
            framebuffer_type = tag->framebuffer_type;
            if (framebuffer_type == 1 && header->size >= 32) {
                red_position = tag->color_info[0];
                red_mask = tag->color_info[1];
                green_position = tag->color_info[2];
                green_mask = tag->color_info[3];
                blue_position = tag->color_info[4];
                blue_mask = tag->color_info[5];
            }
            if (framebuffer_type != 1 || (framebuffer_bpp != 24 && framebuffer_bpp != 32)) framebuffer = 0;
            return;
        }
        if (header->size < 8) break;
        cursor += (header->size + 7u) & ~7u;
    }
    if (!force_framebuffer) framebuffer = 0;
}

void display_flush_framebuffer(void) {
    if (!framebuffer) return;
    for (uint32_t row = 0; row < SCREEN_ROWS; row++) {
        for (uint32_t col = 0; col < SCREEN_COLS; col++) {
            char ch = text_shadow[(row * SCREEN_COLS + col) * 2];
            unsigned char color = (unsigned char)text_shadow[(row * SCREEN_COLS + col) * 2 + 1];
            framebuffer_draw_char(col, row, ch, color);
        }
    }
}

char* display_text_buffer(void) {
    return framebuffer ? text_shadow : (char*)0xB8000;
}

uint32_t display_framebuffer_start(void) {
    return framebuffer ? (uint32_t)(uintptr_t)framebuffer : 0;
}

uint32_t display_framebuffer_end(void) {
    if (!framebuffer) return 0;
    return (uint32_t)(uintptr_t)framebuffer + framebuffer_pitch * framebuffer_height;
}

static char scrollback_buffer[SCROLLBACK_LINES][SCREEN_COLS * CELL_BYTES];
static int scrollback_start = 0;
static int scrollback_count = 0;
static int scrollback_offset = 0;
static char live_screen_snapshot[SCREEN_BYTES];
static char* capture_buffer = 0;
static int capture_max_len = 0;
static int capture_len = 0;
static int capture_suppress_screen = 0;

static int mouse_col = 40;
static int mouse_row = 12;
static int mouse_visible = 0;
static int mouse_saved_offset = -1;
static char mouse_saved_char = ' ';
static unsigned char mouse_saved_attr = COLOR_LIGHT_GRAY;

static void capture_append_char(char c) {
    if (!capture_buffer || capture_max_len <= 0) return;
    if (capture_len >= capture_max_len - 1) return;
    capture_buffer[capture_len++] = c;
    capture_buffer[capture_len] = 0;
}

static void capture_append_text(const char* str, int len, int prepend_newline) {
    if (!capture_buffer || !str) return;

    if (prepend_newline && capture_len > 0) {
        capture_append_char('\n');
    }

    for (int i = 0; i < len; ) {
        if (str[i] == '\\' && (i + 1 < len) && str[i + 1] == 'n') {
            capture_append_char('\n');
            i += 2;
            continue;
        }
        if (str[i] == '\n' || str[i] == 10) {
            capture_append_char('\n');
            i++;
            continue;
        }
        capture_append_char(str[i]);
        i++;
    }
}

static void copy_bytes(char* dst, const char* src, int count) {
    for (int i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

static void save_live_screen(char* video) {
    copy_bytes(live_screen_snapshot, video, SCREEN_BYTES);
}

static void restore_live_screen_bytes(char* video) {
    copy_bytes(video, live_screen_snapshot, SCREEN_BYTES);
}

static void append_scrollback_row(const char* video) {
    int row_index = (scrollback_start + scrollback_count) % SCROLLBACK_LINES;
    copy_bytes(scrollback_buffer[row_index], video, SCREEN_COLS * CELL_BYTES);
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    } else {
        scrollback_start = (scrollback_start + 1) % SCROLLBACK_LINES;
    }
}

static const char* get_scrollback_row(int index) {
    if (index < 0 || index >= scrollback_count) return 0;
    return scrollback_buffer[(scrollback_start + index) % SCROLLBACK_LINES];
}

static void clear_mouse_overlay(char* video) {
    if (!mouse_visible || mouse_saved_offset < 0) return;
    video[mouse_saved_offset] = mouse_saved_char;
    video[mouse_saved_offset + 1] = mouse_saved_attr;
    mouse_visible = 0;
    mouse_saved_offset = -1;
}

static void draw_mouse_overlay(char* video) {
    int cell = mouse_row * SCREEN_COLS + mouse_col;
    int offset = cell * CELL_BYTES;
    mouse_saved_char = video[offset];
    mouse_saved_attr = (unsigned char)video[offset + 1];
    mouse_saved_offset = offset;
    video[offset] = ' '; //all we use it for is scrolling so i got rid of the X, can put it back later when we have a real GUI
    video[offset + 1] = COLOR_LIGHT_CYAN;
    mouse_visible = 1;
}

static void render_scrollback_view(char* video) {
    int top_line = scrollback_count - scrollback_offset;
    if (top_line < 0) top_line = 0;

    for (int row = 0; row < SCREEN_ROWS; row++) {
        int source_line = top_line + row;
        int dst_offset = row * SCREEN_COLS * CELL_BYTES;

        if (source_line < scrollback_count) {
            const char* src = get_scrollback_row(source_line);
            if (src) {
                copy_bytes(&video[dst_offset], src, SCREEN_COLS * CELL_BYTES);
                continue;
            }
        }

        source_line -= scrollback_count;
        if (source_line >= 0 && source_line < SCREEN_ROWS) {
            copy_bytes(&video[dst_offset], &live_screen_snapshot[source_line * SCREEN_COLS * CELL_BYTES], SCREEN_COLS * CELL_BYTES);
        } else {
            for (int col = 0; col < SCREEN_COLS; col++) {
                video[dst_offset + col * CELL_BYTES] = ' ';
                video[dst_offset + col * CELL_BYTES + 1] = COLOR_LIGHT_GRAY;
            }
        }
    }
}

void display_init(char* video) {
    scrollback_start = 0;
    scrollback_count = 0;
    scrollback_offset = 0;
    mouse_visible = 0;
    mouse_saved_offset = -1;
    save_live_screen(video);
    display_flush_framebuffer();
}

void display_hide_mouse(char* video) {
    clear_mouse_overlay(video);
}

void display_refresh_mouse(char* video) {
    int old_offset = mouse_saved_offset;
    clear_mouse_overlay(video);
    draw_mouse_overlay(video);
    if (old_offset >= 0) {
        int old_cell = old_offset / CELL_BYTES;
        display_flush_framebuffer_cell(old_cell % SCREEN_COLS, old_cell / SCREEN_COLS);
    }
    display_flush_framebuffer_cell(mouse_col, mouse_row);
}

void display_set_mouse_position(int col, int row) {
    if (col < 0) col = 0;
    if (col >= SCREEN_COLS) col = SCREEN_COLS - 1;
    if (row < 0) row = 0;
    if (row >= SCREEN_ROWS) row = SCREEN_ROWS - 1;
    mouse_col = col;
    mouse_row = row;
}

int display_scroll_view(int delta, char* video) {
    if (scrollback_count <= 0) {
        scrollback_offset = 0;
        return 0;
    }

    int next_offset = scrollback_offset - delta; //make it + delta to swithc scroll direction
    if (next_offset < 0) next_offset = 0;
    if (next_offset > scrollback_count) next_offset = scrollback_count;

    if (scrollback_offset == 0 && next_offset > 0) {
        clear_mouse_overlay(video);
        save_live_screen(video);
    }

    scrollback_offset = next_offset;
    if (scrollback_offset == 0) {
        restore_live_screen_bytes(video);
    } else {
        render_scrollback_view(video);
    }

    return scrollback_offset;
}

int display_is_scrollback_active(void) {
    return scrollback_offset > 0;
}

void display_sync_live_screen(char* video) {
    if (scrollback_offset != 0) return;
    clear_mouse_overlay(video);
    save_live_screen(video);
    display_flush_framebuffer();
}

void display_restore_live_screen(char* video) {
    scrollback_offset = 0;
    clear_mouse_overlay(video);
    restore_live_screen_bytes(video);
    display_flush_framebuffer();
}

void display_begin_capture(char* buffer, int max_len, int suppress_screen) {
    capture_buffer = buffer;
    capture_max_len = max_len;
    capture_len = 0;
    capture_suppress_screen = suppress_screen;
    if (capture_buffer && capture_max_len > 0) {
        capture_buffer[0] = 0;
    }
}

int display_end_capture(void) {
    int out_len = capture_len;
    capture_buffer = 0;
    capture_max_len = 0;
    capture_len = 0;
    capture_suppress_screen = 0;
    return out_len;
}

void scroll_screen(char* video) {
    clear_mouse_overlay(video);
    append_scrollback_row(video);
    //move all lines up by one
    for (int row = 1; row < SCREEN_ROWS; row++) {
        for (int col = 0; col < SCREEN_COLS; col++) {
            video[((row-1)*SCREEN_COLS+col)*2] = video[(row*SCREEN_COLS+col)*2];
            video[((row-1)*SCREEN_COLS+col)*2+1] = video[(row*SCREEN_COLS+col)*2+1];
        }
    }
    // clear last line
    for (int col = 0; col < SCREEN_COLS; col++) {
        video[((SCREEN_ROWS-1)*SCREEN_COLS+col)*2] = ' ';
        video[((SCREEN_ROWS-1)*SCREEN_COLS+col)*2+1] = 0x07;
    }
}

void print_smiggles_art(char* video, int* cursor) {
    const char* smiggles_art[6] = {
        "               _             _           ",
        " ___ _ __ ___ (_) __ _  __ _| | ___  ___ ",
        "/ __| '_ ` _ \\| |/ _` |/ _` | |/ _ \\/ __|",
        "\\__ \\ | | | | | | (_| | (_| | |  __/\\__ \\",
        "|___/_| |_| |_|_|\\__, |\\__, |_|\\___||___/",
        "                 |___/ |___/             "
    };
    int art_lines = 6;
    for (int l = 0; l < art_lines; l++) {
        for (int j = 0; smiggles_art[l][j] && j < 80; j++) {
            video[(l*80+j)*2] = smiggles_art[l][j];
            video[(l*80+j)*2+1] = COLOR_GREEN;
        }
    }
    *cursor = art_lines * 80;
}

void print_boot_splash(char* video) {
    // Clear screen
    for (int i = 0; i < 80*25*2; i += 2) {
        video[i] = ' ';
        video[i + 1] = COLOR_BLACK;
    }
    
    // Snake logo (left side)
    const char* snake_logo[12] = {
        "",
        "",
        "           /^/^-\\",
        "         _|__|  O|",
        "\\/     /~     \\_/ \\",
        " \\____|__________/  \\",
        "        \\_______      \\",
        "                `\\     \\",
        "                  |     |",
        "                 /      /",
        "                /     /",
        "              /      /"
    };
    
    // Smiggles ASCII art (right side)
    const char* smiggles_art[6] = {
        "               _             _           ",
        " ___ _ __ ___ (_) __ _  __ _| | ___  ___ ",
        "/ __| '_ ` _ \\| |/ _` |/ _` | |/ _ \\/ __|",
        "\\__ \\ | | | | | | (_| | (_| | |  __/\\__ \\",
        "|___/_| |_| |_|_|\\__, |\\__, |_|\\___||___/",
        "                 |___/ |___/             "
    };
    
    // Center the combined artwork on screen
    // Snake is ~20 chars wide, smiggles is ~42 chars wide, gap is ~4 chars
    // Total: ~66 chars, centered on 80 char width = starting at column 7
    int start_col = 6;
    int start_row = 5;
    
    // Print snake logo (left part)
    for (int l = 0; l < 12; l++) {
        for (int j = 0; snake_logo[l][j] && j < 25; j++) {
            int col = start_col + j;
            if (col < 80) {
                int offset = ((start_row - 1 + l) * 80 + col) * 2;
                video[offset] = snake_logo[l][j];
                video[offset + 1] = COLOR_GREEN;
            }
        }
    }
    
    // Print smiggles art on right (with gap after snake)
    int smiggles_start_col = start_col + 25 + 4;  // 25 for snake + 4 for gap
    for (int l = 0; l < 6; l++) {
        for (int j = 0; smiggles_art[l][j] && j < 42; j++) {
            int col = smiggles_start_col + j;
            if (col < 80) {
                int offset = ((start_row + l) * 80 + col) * 2;
                video[offset] = smiggles_art[l][j];
                video[offset + 1] = COLOR_GREEN;
            }
        }
    }
    
    // Print boot message at bottom
    const char* boot_msg_base = "Booting Smiggles OS";
    int msg_col = (80 - 19) / 2;  // Center the message
    for (int j = 0; boot_msg_base[j] && j < 19; j++) {
        int offset = (20 * 80 + msg_col + j) * 2;
        video[offset] = boot_msg_base[j];
        video[offset + 1] = COLOR_GREEN;
    }
}

//print a string on NEW LINE with color
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color) {
    int computed_len = len;
    if (computed_len < 0) {
        computed_len = 0;
        while (str[computed_len]) computed_len++;
    }

    if (capture_buffer) {
        capture_append_text(str, computed_len, 1);
        if (capture_suppress_screen) return;
    }

    *cursor = ((*cursor / 80) + 1) * 80; //this is what goes to the new line
    while (*cursor >= 80*25) {
        scroll_screen(video);
        *cursor -= 80;
    }
    // If len < 0, auto-calculate string length
    len = computed_len;
    for (int i = 0; i < len; ) {
        // Handle "\\n" (two-character sequence)
        if (str[i] == '\\' && (i+1 < len) && str[i+1] == 'n') {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i += 2;
            continue;
        }
        // Handle actual newline character (char 10)
        if (str[i] == '\n' || str[i] == 10) {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i++;
            continue;
        }
        while (*cursor >= 80*25) {
            scroll_screen(video);
            *cursor -= 80;
        }
        video[(*cursor)*2] = str[i];
        video[(*cursor)*2+1] = color;
        (*cursor)++;
        i++;
    }
    if (computed_len <= 1) {
        display_flush_framebuffer_cell((*cursor) - 1, *cursor / SCREEN_COLS);
    } else {
        display_flush_framebuffer();
    }
}

//print string on SAME LINE with color
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color) {
    int computed_len = len;
    if (computed_len < 0) {
        computed_len = 0;
        while (str[computed_len]) computed_len++;
    }

    if (capture_buffer) {
        capture_append_text(str, computed_len, 0);
        if (capture_suppress_screen) return;
    }

    // If len < 0, auto-calculate string length
    len = computed_len;
    for (int i = 0; i < len; ) {
        // Handle "\\n" (two-character sequence)
        if (str[i] == '\\' && (i+1 < len) && str[i+1] == 'n') {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i += 2;
            continue;
        }
        // Handle actual newline character (char 10)
        if (str[i] == '\n' || str[i] == 10) {
            *cursor = ((*cursor / 80) + 1) * 80;
            while (*cursor >= 80*25) {
                scroll_screen(video);
                *cursor -= 80;
            }
            i++;
            continue;
        }
        while (*cursor >= 80*25) {
            scroll_screen(video);
            *cursor -= 80;
        }
        video[(*cursor)*2] = str[i];
        video[(*cursor)*2+1] = color;
        (*cursor)++;
        i++;
    }
    if (computed_len <= 1) {
        display_flush_framebuffer_cell((*cursor) - 1, *cursor / SCREEN_COLS);
    } else {
        display_flush_framebuffer();
    }
}

// --- Shared Keyboard and Display Utilities ---
// Simple get_key implementation for login input
char get_key(void) {
    unsigned char scancode = 0;
    int shift = 0;
    int e0_prefix_pending = 0;
    while (1) {
        if (keyboard_pop_scancode(&scancode)) {
            // Handle shift
            if (scancode == 0x2A || scancode == 0x36) { shift = 1; continue; }
            if (scancode == 0xAA || scancode == 0xB6) { shift = 0; continue; }

            // Handle E0-prefixed extended keys (e.g. numpad Enter)
            if (scancode == 0xE0) {
                e0_prefix_pending = 1;
                continue;
            }

            if (e0_prefix_pending) {
                e0_prefix_pending = 0;
                if (scancode & 0x80) continue;
                if (scancode == 0x1C) return '\n';
                if (scancode == 0x35) return '/';
                continue;
            }

            // Ignore release codes
            if (scancode > 0x80) continue;
            char c = scancode_to_char(scancode, shift);
            if (c) return c;
        }
    }
}

// Scancode to character conversion tables
static const char lower_table[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ', [0x1C] = '\n', [0x0E] = 8,
    [0x0F] = '\t',
    [0x37] = '*', [0x4A] = '-', [0x4E] = '+',
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.',
};

static const char upper_table[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ', [0x0E] = 8,
    [0x0F] = '\t',
    [0x37] = '*', [0x4A] = '-', [0x4E] = '+',
    [0x47] = '7', [0x48] = '8', [0x49] = '9',
    [0x4B] = '4', [0x4C] = '5', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3',
    [0x52] = '0', [0x53] = '.',
};

// Set VGA hardware cursor position
void set_cursor_position(int cursor) {
    unsigned short pos = cursor;
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

// --- Numpad scancode handling ---
// Only map numpad keys if not E0-prefixed (arrow keys, etc. use E0)
char scancode_to_char(unsigned char scancode, int shift) {
    if (shift) {
        return upper_table[scancode];
    } else {
        return lower_table[scancode];
    }
}

void display_flush_framebuffer_cell(int col, int row) {
    if (!framebuffer || col < 0 || col >= SCREEN_COLS || row < 0 || row >= SCREEN_ROWS) return;
    framebuffer_draw_char((uint32_t)col, (uint32_t)row,
        text_shadow[(row * SCREEN_COLS + col) * 2],
        (unsigned char)text_shadow[(row * SCREEN_COLS + col) * 2 + 1]);
}