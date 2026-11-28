#include "kernel.h"
#include "include/display.h"

#define GUI_GRAY 0xC0C0C0
#define GUI_DARK 0x808080
#define GUI_LIGHT 0xFFFFFF
#define GUI_BLACK 0x000000
#define GUI_DESKTOP 0x008080
#define GUI_BLUE 0x000080
#define GUI_TOPBAR_HEIGHT 48
#define GUI_MAX_WINDOWS 8
#define GUI_TEXT_CAPACITY 2048
typedef struct {
    int used;
    int type;
    int x;
    int y;
    int w;
    int h;
    int maximized;
    int restore_x;
    int restore_y;
    int restore_w;
    int restore_h;
    int z;
    char title[24];
} GuiWindow;
typedef struct {
    char text[GUI_TEXT_CAPACITY];
    int length;
    int cursor;
} GuiDocument;

static GuiDocument documents[GUI_MAX_WINDOWS];
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

static void gui_draw_topbar(uint32_t screen_width) {
    display_draw_rect(0u, 0u, screen_width, GUI_TOPBAR_HEIGHT, GUI_GRAY);
    display_draw_rect(0u, 0u, screen_width, 2u, GUI_LIGHT);
    display_draw_rect(0u, GUI_TOPBAR_HEIGHT - 2,
                      screen_width, 2u, GUI_DARK);

    gui_draw_button(8, 10, 82, 28);
    display_draw_text(18u, 19u, "Start", GUI_BLACK);
    display_draw_text(106u, 19u, "Smiggles", GUI_BLACK);
    display_draw_text(234u, 19u, "Applications", GUI_BLACK);
    display_draw_text(400u, 19u, "Help", GUI_BLACK);

    char time_buf[16];
    time_buf[0] = 0;
    get_time_string(time_buf);
    if (time_buf[0]) {
        int len = 0;
        while (time_buf[len]) len++;
        int text_x = (int)screen_width - len * 9 - 56;
        if (text_x > 500) display_draw_text((uint32_t)text_x, 19u,
                                            time_buf, GUI_BLACK);
    }
}

static void gui_draw_start_menu(int open) {
    if (!open) return;
    int x = 8;
    int y = GUI_TOPBAR_HEIGHT;
    int w = 176;
    int h = 72;
    display_draw_rect((uint32_t)x, (uint32_t)y,
                      (uint32_t)w, (uint32_t)h, GUI_GRAY);
    gui_draw_beveled_rect(x, y, w, h, GUI_LIGHT, GUI_BLACK);
    display_draw_text((uint32_t)(x + 10), (uint32_t)(y + 10),
                      "New Window", GUI_BLACK);
    display_draw_text((uint32_t)(x + 10), (uint32_t)(y + 29),
                      "Exit GUI", GUI_BLACK);
    display_draw_line((uint32_t)(x + 4), (uint32_t)(y + 44),
                      (uint32_t)(x + w - 4), (uint32_t)(y + 44),
                      GUI_DARK);
}
static void gui_draw_applications_menu(int open) {
    if (!open) return;

    int x = 220;
    int y = GUI_TOPBAR_HEIGHT;
    int w = 176;
    int h = 44;

    display_draw_rect(x, y, w, h, GUI_GRAY);
    gui_draw_beveled_rect(x, y, w, h, GUI_LIGHT, GUI_BLACK);

    display_draw_text(x + 10, y + 13, "Notepad", GUI_BLACK);
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

    int max_x = wx + w - 38;
    gui_draw_button(max_x, wy + 4, 16, 14);
    gui_draw_inset(max_x + 4, wy + 7, 8, 7);

    int resize_grip_x = wx + w - 10;
    int resize_grip_y = wy + h - 10;
    for (int i = 0; i < 3; i++) {
        display_draw_line((uint32_t)(resize_grip_x + i * 3),
                          (uint32_t)(resize_grip_y + 6),
                          (uint32_t)(resize_grip_x + 6),
                          (uint32_t)(resize_grip_y + i * 3),
                          GUI_DARK);
    }

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
static void gui_draw_notepad(int wx, int wy, int w, int h,
                             uint32_t screen_width,
                             uint32_t screen_height,
                             GuiDocument* document) {
    gui_draw_window(wx, wy, w, h, screen_width, screen_height);
    display_draw_rect((uint32_t)(wx + 9),
                      (uint32_t)(wy + 35),
                      (uint32_t)(w - 18),
                      (uint32_t)(h - 44),
                      0xFFFFFF);

    int text_x = wx + 14;
    int text_y = wy + 39;
    int max_columns = (w - 32) / 9;
    int max_rows = (h - 52) / 16;

    if (max_columns > 127) max_columns = 127;

    if (max_columns <= 0 || max_rows <= 0) return;

    char line[128];
    int column = 0;
    int row = 0;

    for (int i = 0; i <= document->length && row < max_rows; i++) {
        char c = document->text[i];

        if (c == '\n' || c == '\0' || column >= max_columns) {
            line[column] = '\0';

            display_draw_text((uint32_t)text_x,
                              (uint32_t)(text_y + row * 16),
                              line, GUI_BLACK);

            column = 0;
            row++;

            if (c == '\0') break;
            if (c == '\n') continue;
        }

        if (row < max_rows && c != '\n' && c != '\0') {
            line[column++] = c;
        }
    }
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

    GuiWindow windows[GUI_MAX_WINDOWS];
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        windows[i].used = 0;
        documents[i].length = 0;
        documents[i].cursor = 0;
        documents[i].text[0] = '\0';
    }
    int active = 0;
    int next_z = 1;
    windows[0].used = 1;
    windows[0].type = 0;
    windows[0].x = (int)screen_width / 4;
    windows[0].y = (int)screen_height / 4;
    windows[0].w = 320;
    windows[0].h = 200;
    if (windows[0].w > (int)screen_width) windows[0].w = (int)screen_width;
    if (windows[0].h > (int)screen_height) windows[0].h = (int)screen_height;
    windows[0].maximized = 0;
    windows[0].z = next_z++;
    int dragging = 0;
    int resizing = 0;
    int drag_dx = 0;
    int drag_dy = 0;
    int resize_start_x = 0;
    int resize_start_y = 0;
    int resize_start_w = 0;
    int resize_start_h = 0;
    int start_menu_open = 0;
    int applications_menu_open = 0;
    int shift = 0;
    int mouse_x = (int)screen_width / 2;
    int mouse_y = (int)screen_height / 2;

    while (1) {
        MouseState mouse;
        unsigned char scancode;

        display_draw_rect(0u, 0u, screen_width, screen_height, GUI_DESKTOP);
        gui_draw_topbar(screen_width);
        for (int z = 1; z < next_z; z++) {
            for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
                if (windows[i].used && windows[i].z == z) {
                    if (windows[i].type == 0) {
                        gui_draw_window(windows[i].x, windows[i].y,
                            windows[i].w, windows[i].h,
                            screen_width, screen_height);
                    } else {
                        gui_draw_notepad(windows[i].x, windows[i].y,
                            windows[i].w, windows[i].h,
                            screen_width, screen_height,
                            &documents[i]);
                    }
                }
            }
        }
        gui_draw_start_menu(start_menu_open);
        gui_draw_applications_menu(applications_menu_open);
        gui_draw_cursor(mouse_x, mouse_y);
        display_present_framebuffer();

        if (mouse_poll_state(&mouse)) {
            mouse_x = mouse.x;
            mouse_y = mouse.y;

            if ((mouse.changed_buttons & 1u) &&
                (mouse.buttons & 1u)) {
                if (gui_rect_contains(mouse_x, mouse_y, 8, 10, 82, 28)) {
                    start_menu_open = !start_menu_open;
                    applications_menu_open = 0;
                } else if (gui_rect_contains(mouse_x, mouse_y, 224, 10, 126, 28)) {
                    applications_menu_open = !applications_menu_open;
                    start_menu_open = 0;
                } else if (applications_menu_open && gui_rect_contains(mouse_x, mouse_y, 220, GUI_TOPBAR_HEIGHT, 176, 44)) {
                    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
                        if (!windows[i].used) {
                            windows[i].used = 1;
                            windows[i].type = 1;
                            windows[i].x = 100 + i * 18;
                            windows[i].y = 80 + i * 18;
                            windows[i].w = 320;
                            windows[i].h = 200;
                            windows[i].maximized = 0;
                            windows[i].z = next_z++;
                            documents[i].length = 0;
                            documents[i].cursor = 0;
                            documents[i].text[0] = '\0';
                            active = i;
                            break;
                        }
                    }
                applications_menu_open = 0;
                } else if (applications_menu_open) {
                    applications_menu_open = 0;
                } else if (start_menu_open) {
                    if (gui_rect_contains(mouse_x, mouse_y, 8,GUI_TOPBAR_HEIGHT, 176, 72)) {
                        if (mouse_y < GUI_TOPBAR_HEIGHT + 38) {
                            for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
                                if (!windows[i].used) {
                                    windows[i].used = 1;
                                    windows[i].type = 0;
                                    windows[i].x = 100 + i * 18;
                                    windows[i].y = 80 + i * 18;
                                    windows[i].w = 320;
                                    windows[i].h = 200;
                                    windows[i].maximized = 0;
                                    windows[i].z = next_z++;
                                    active = i;
                                    break;
                                }
                            }
                        } else {
                            break;
                        }
                    }
                    start_menu_open = 0;
                } else {
                    start_menu_open = 0;
                    active = -1;
                    for (int z = next_z - 1; z >= 1 && active < 0; z--) {
                        for (int i = GUI_MAX_WINDOWS - 1; i >= 0; i--) {
                            if (!windows[i].used || windows[i].z != z) continue;
                            GuiWindow* w = &windows[i];
                            if (gui_rect_contains(mouse_x, mouse_y,
                                                  w->x, w->y, w->w, w->h)) {
                                active = i;
                                w->z = next_z++;
                            }
                        }
                    }
                    if (active >= 0) {
                        GuiWindow* w = &windows[active];
                        int close_hit = gui_rect_contains(mouse_x, mouse_y,
                                                          w->x + w->w - 20,
                                                          w->y + 4, 16, 14);
                        int max_hit = gui_rect_contains(mouse_x, mouse_y,
                                                        w->x + w->w - 38,
                                                        w->y + 4, 16, 14);
                        int resize_hit = !w->maximized &&
                                         gui_rect_contains(mouse_x, mouse_y,
                                                           w->x + w->w - 14,
                                                           w->y + w->h - 14, 14, 14);
                        int title_hit = gui_rect_contains(mouse_x, mouse_y,
                                                           w->x, w->y, w->w, 22);
                        if (close_hit) {
                            w->used = 0;
                        } else if (max_hit) {
                            if (w->maximized) {
                                w->x = w->restore_x;
                                w->y = w->restore_y;
                                w->w = w->restore_w;
                                w->h = w->restore_h;
                                w->maximized = 0;
                            } else {
                                w->restore_x = w->x;
                                w->restore_y = w->y;
                                w->restore_w = w->w;
                                w->restore_h = w->h;
                                w->x = 0;
                                w->y = GUI_TOPBAR_HEIGHT;
                                w->w = (int)screen_width;
                                w->h = (int)screen_height - GUI_TOPBAR_HEIGHT;
                                w->maximized = 1;
                            }
                        } else if (resize_hit) {
                            resizing = 1;
                            resize_start_x = mouse_x;
                            resize_start_y = mouse_y;
                            resize_start_w = w->w;
                            resize_start_h = w->h;
                        } else if (title_hit) {
                            dragging = 1;
                            drag_dx = mouse_x - w->x;
                            drag_dy = mouse_y - w->y;
                        }
                    }
                }
            }
            if ((mouse.changed_buttons & 1u) &&
                !(mouse.buttons & 1u)) {
                dragging = 0;
                resizing = 0;
            }
            if (active >= 0 && dragging && !windows[active].maximized && (mouse.buttons & 1u)) {
                GuiWindow* w = &windows[active];
                w->x = mouse_x - drag_dx;
                w->y = mouse_y - drag_dy;
                if (w->x < 0) w->x = 0;
                if (w->y < GUI_TOPBAR_HEIGHT) w->y = GUI_TOPBAR_HEIGHT;
                if (w->x + w->w > (int)screen_width) w->x = (int)screen_width - w->w;
                if (w->y + w->h > (int)screen_height) w->y = (int)screen_height - w->h;
            }
            if (active >= 0 && resizing && (mouse.buttons & 1u)) {
                GuiWindow* w = &windows[active];
                w->w = resize_start_w + mouse_x - resize_start_x;
                w->h = resize_start_h + mouse_y - resize_start_y;
                if (w->w < 180) w->w = 180;
                if (w->h < 120) w->h = 120;
                if (w->x + w->w > (int)screen_width) w->w = (int)screen_width - w->x;
                if (w->y + w->h > (int)screen_height) w->h = (int)screen_height - w->y;
            }
        }
        if (keyboard_pop_scancode(&scancode)) {
            if (scancode == 0x01) break;
            if (scancode == 0x2A || scancode == 0x36) 
                shift = 1;
            else if (scancode == 0xAA || scancode == 0xB6) 
                shift = 0;
            else if (!(scancode & 0x80) &&
                active >= 0 &&
                windows[active].used &&
                windows[active].type == 1) {
            GuiDocument* doc = &documents[active];
            if (scancode == 0x0E) {
                if (doc->cursor > 0) {
                    doc->cursor--;
                    doc->length--;
                    for (int j = doc->cursor; j < doc->length; j++) 
                        doc->text[j] = doc->text[j + 1];
                    doc->text[doc->length] = '\0';
            }
            } else if (scancode == 0x1C) {
                if (doc->length < GUI_TEXT_CAPACITY - 1) {
                    for (int j = doc->length; j > doc->cursor; j--) 
                        doc->text[j] = doc->text[j - 1];

                    doc->text[doc->cursor] = '\n';
                    doc->cursor++;
                    doc->length++;
                    doc->text[doc->length] = '\0';
                }
            } else {
                char c = scancode_to_char(scancode, shift);
                if (c >= 32 && c <= 126 && doc->length < GUI_TEXT_CAPACITY - 1) {
                    for (int j = doc->length; j > doc->cursor; j--) 
                        doc->text[j] = doc->text[j - 1];
                    doc->text[doc->cursor] = c;
                    doc->cursor++;
                    doc->length++;
                    doc->text[doc->length] = '\0';
                }
            }
        }
    }
        asm volatile("hlt");
    }

    display_clear_framebuffer();
    display_flush_framebuffer();
    display_sync_live_screen(video);
    display_refresh_mouse(video);
}
