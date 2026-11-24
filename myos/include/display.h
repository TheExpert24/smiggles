#ifndef SMIGGLES_DISPLAY_H
#define SMIGGLES_DISPLAY_H

#include <stdint.h>

void scroll_screen(char* video);
void print_string(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);
void print_smiggles_art(char* video, int* cursor);
void print_boot_splash(char* video);
void set_cursor_position(int cursor);
char scancode_to_char(unsigned char scancode, int shift);
void display_init(char* video);
void display_set_multiboot_framebuffer(unsigned int mb_magic, unsigned int mb_info_addr);
void display_flush_framebuffer(void);
void display_flush_framebuffer_cell(int col, int row);
char* display_text_buffer(void);
uint32_t display_framebuffer_start(void);
uint32_t display_framebuffer_end(void);
int display_framebuffer_available(void);
void display_hide_mouse(char* video);
void display_refresh_mouse(char* video);
void display_set_mouse_position(int col, int row);
int display_scroll_view(int delta, char* video);
int display_is_scrollback_active(void);
void display_sync_live_screen(char* video);
void display_restore_live_screen(char* video);
void gui_run_test_window(void);
void display_get_framebuffer_info(uint32_t* out_width,
                                  uint32_t* out_height,
                                  uint32_t* out_pitch,
                                  uint32_t* out_bpp);
void display_draw_pixel(uint32_t x, uint32_t y, uint32_t rgb);
void display_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t rgb);
void display_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t rgb);
void display_present_framebuffer(void);
void display_clear_framebuffer(void);

#endif
