#ifndef WINDOW_H
#define WINDOW_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

typedef void (*window_key_cb_t)(uint32_t vk, bool down, void *userdata);
typedef void (*window_mouse_cb_t)(int32_t x, int32_t y, uint32_t buttons,
                                   uint32_t action, void *userdata);
typedef void (*window_wheel_cb_t)(int32_t x, int32_t y, int32_t delta, void *userdata);
typedef void (*window_resize_cb_t)(int32_t width, int32_t height, void *userdata);

typedef struct {
    window_key_cb_t key_cb;
    window_mouse_cb_t mouse_cb;
    window_wheel_cb_t wheel_cb;
    window_resize_cb_t resize_cb;
    void *userdata;
} window_callbacks_t;

typedef struct {
    HWND hwnd;
    int width;
    int height;
    bool fullscreen;
    bool always_on_top;
    window_callbacks_t callbacks;
} window_t;

bool window_init(window_t *win, HINSTANCE hInstance, const char *title,
                 int width, int height);
void window_set_callbacks(window_t *win, const window_callbacks_t *callbacks);
void window_show(window_t *win);
void window_set_fullscreen(window_t *win, bool fullscreen);
void window_set_always_on_top(window_t *win, bool always_on_top);
void window_destroy(window_t *win);

#endif /* WINDOW_H */
