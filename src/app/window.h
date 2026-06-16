#ifndef WINDOW_H
#define WINDOW_H

#include <windows.h>
#include <stdbool.h>

typedef struct {
    HWND hwnd;
    int width;
    int height;
    bool fullscreen;
    bool always_on_top;
} window_t;

bool window_init(window_t *win, HINSTANCE hInstance, const char *title,
                 int width, int height);
void window_show(window_t *win);
void window_set_fullscreen(window_t *win, bool fullscreen);
void window_set_always_on_top(window_t *win, bool always_on_top);
void window_destroy(window_t *win);

#endif /* WINDOW_H */
