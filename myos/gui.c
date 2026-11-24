#include "kernel.h"
#include "include/display.h"

#define GUI_GRAY 0xC0C0C0
#define GUI_DARK 0x808080
#define GUI_LIGHT 0xFFFFFF
#define GUI_BLACK 0x000000
#define GUI_DESKTOP 0x008080
#define GUI_BLUE 0x000080

static void gui_draw_beveled_rect(int x, int y, int w, int h,
                                  uint32_t light, uint32_t dark) {
    display_draw_rect((uint32_t)x, (uint32_t)y,
                      (uint32_t)w, 1u, light);
    display_draw_rect((uint32_t)x, (uint32_t)y,
                      1u, (uint32_t)h, light);
    display_draw_rect((uint32_t)(x + w - 1), (uint32_t)y,
                      1u, (uint32_t)h, dark);
    display_draw_rect((uint32_t)x, (uint32_t)(y + h - 1),
                      (uint32_t)w, 1u, dark);
}

static void gui_draw_inset(int x, int y, int w, int h) {
    gui_draw_beveled_rect(x, y, w, h, GUI_DARK, GUI_LIGHT);
}

static void gui_draw_button(int x, int y, int w, int h) {
    display_draw_rect((uint32_t)x, (uint32_t)y,
                      (uint32_t)w, (uint32_t)h, GUI_GRAY);
    gui_draw_beveled_rect(x, y, w, h, GUI_LIGHT, GUI_BLACK);
    gui_draw_beveled_rect(x + 1, y + 1, w - 2, h - 2,
                          GUI_GRAY, GUI_DARK);
}

static int gui_rect_contains(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static void gui_draw_cursor(int x, int y) {
    display_draw_rect((uint32_t)x, (uint32_t)y, 12u, 16u, GUI_LIGHT);
    display_draw_line((uint32_t)x, (uint32_t)y,
                      (uint32_t)x, (uint32_t)(y + 15), GUI_BLACK);
    display_draw_line((uint32_t)x, (uint32_t)y,
                      (uint32_t)(x + 11), (uint32_t)(y + 11), GUI_BLACK);
    display_draw_line((uint32_t)(x + 11), (uint32_t)(y + 11),
                      (uint32_t)(x + 7), (uint32_t)(y + 11), GUI_BLACK);
    display_draw_line((uint32_t)(x + 7), (uint32_t)(y + 11),
                      (uint32_t)x, (uint32_t)(y + 15), GUI_BLACK);
}

static void gui_draw_window(int wx, int wy, int w, int h,
                            uint32_t screen_width, uint32_t screen_height) {
    display_draw_rect(0u, 0u, screen_width, screen_height, GUI_DESKTOP);

    if (wx + w + 3 < (int)screen_width && wy + h + 3 < (int)screen_height) {
        display_draw_rect((uint32_t)(wx + 3), (uint32_t)(wy + 3),
                          (uint32_t)w, (uint32_t)h, 0x404040);
    }

    display_draw_rect((uint32_t)wx, (uint32_t)wy,
                      (uint32_t)w, (uint32_t)h, GUI_GRAY);
    gui_draw_beveled_rect(wx, wy, w, h, GUI_LIGHT, GUI_BLACK);
    gui_draw_beveled_rect(wx + 2, wy + 2, w - 4, h - 4,
                          GUI_LIGHT, GUI_DARK);

    display_draw_rect((uint32_t)(wx + 4), (uint32_t)(wy + 4),
                      (uint32_t)(w - 8), 18u, GUI_BLUE);
    display_draw_text((uint32_t)(wx + 8), (uint32_t)(wy + 5),
                      "Smiggles", GUI_LIGHT);

    int close_x = wx + w - 20;
    gui_draw_button(close_x, wy + 4, 16, 14);
    display_draw_line((uint32_t)(close_x + 4), (uint32_t)(wy + 8),
                      (uint32_t)(close_x + 11), (uint32_t)(wy + 11), GUI_BLACK);
    display_draw_line((uint32_t)(close_x + 11), (uint32_t)(wy + 8),
                      (uint32_t)(close_x + 4), (uint32_t)(wy + 11), GUI_BLACK);

    gui_draw_inset(wx + 8, wy + 34, w - 16, h - 42);
    display_draw_rect((uint32_t)(wx + 9), (uint32_t)(wy + 35),
                      (uint32_t)(w - 18), (uint32_t)(h - 44), 0xF4F4F4);

    display_draw_rect((uint32_t)(wx + 16), (uint32_t)(wy + 42),
                      (uint32_t)(w - 32), 2u, GUI_DARK);
    display_draw_rect((uint32_t)(wx + 16), (uint32_t)(wy + 52),
                      (uint32_t)(w - 32), 2u, GUI_DARK);

    gui_draw_button(wx + 16, wy + h - 34, 96, 20);
    gui_draw_button(wx + 116, wy + h - 34, 96, 20);
    display_draw_text((uint32_t)(wx + 38), (uint32_t)(wy + h - 30),
                      "OK", GUI_BLACK);
    display_draw_text((uint32_t)(wx + 128), (uint32_t)(wy + h - 30),
                      "Cancel", GUI_BLACK);
}

void gui_run_test_window(void) {
    char* video = display_text_buffer();
    display_hide_mouse(video);

    uint32_t screen_width = 0;
    uint32_t screen_height = 0;
    uint32_t pitch = 0;
    uint32_t bpp = 0;
    display_get_framebuffer_info(&screen_width, &screen_height, &pitch, &bpp);
    if (screen_width == 0u || screen_height == 0u || bpp == 0u) {
        return;
    }

    int win_w = 320;
    int win_h = 200;
    if ((uint32_t)win_w > screen_width) win_w = (int)screen_width;
    if ((uint32_t)win_h > screen_height) win_h = (int)screen_height;
    int win_x = ((int)screen_width - win_w) / 2;
    int win_y = ((int)screen_height - win_h) / 2;
    int dragging = 0;
    int drag_dx = 0;
    int drag_dy = 0;
    int mouse_x = (int)screen_width / 2;
    int mouse_y = (int)screen_height / 2;
    gui_draw_window(win_x, win_y, win_w, win_h, screen_width, screen_height);
    gui_draw_cursor(mouse_x, mouse_y);
    display_present_framebuffer();

    while (1) {
        MouseState mouse;
        unsigned char scancode;

        mouse_poll_hardware();
        if (mouse_poll_state(&mouse)) {
            mouse_x = mouse.x;
            mouse_y = mouse.y;

            int title_hit = gui_rect_contains(mouse_x, mouse_y,
                                               win_x, win_y, win_w, 22);
            int close_hit = gui_rect_contains(mouse_x, mouse_y,
                                               win_x + win_w - 20,
                                               win_y + 4, 16, 14);

            if ((mouse.changed_buttons & 1u) &&
                (mouse.buttons & 1u)) {
                if (close_hit) break;
                if (title_hit) {
                    dragging = 1;
                    drag_dx = mouse_x - win_x;
                    drag_dy = mouse_y - win_y;
                }
            }
            if ((mouse.changed_buttons & 1u) &&
                !(mouse.buttons & 1u)) {
                dragging = 0;
            }
            if (dragging && (mouse.buttons & 1u)) {
                win_x = mouse_x - drag_dx;
                win_y = mouse_y - drag_dy;
                if (win_x < 0) win_x = 0;
                if (win_y < 0) win_y = 0;
                if (win_x + win_w > (int)screen_width) win_x = (int)screen_width - win_w;
                if (win_y + win_h > (int)screen_height) win_y = (int)screen_height - win_h;
            }

            gui_draw_window(win_x, win_y, win_w, win_h, screen_width, screen_height);
            gui_draw_cursor(mouse_x, mouse_y);
            display_present_framebuffer();
        }

        if (keyboard_pop_scancode(&scancode)) {
            if (scancode == 0x01) break;
            if (scancode == 0x3C) {
                gui_draw_window(win_x, win_y, win_w, win_h, screen_width, screen_height);
                gui_draw_cursor(mouse_x, mouse_y);
                display_present_framebuffer();
            }
        }

        asm volatile("hlt");
    }

    display_clear_framebuffer();
    display_flush_framebuffer();
    display_sync_live_screen(video);
    display_refresh_mouse(video);
}
