#include "window.h"
#include "../platform/log.h"

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool window_init(window_t *win, HINSTANCE hInstance, const char *title,
                 int width, int height) {
    win->width = width;
    win->height = height;
    win->fullscreen = false;
    win->always_on_top = false;

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"AutoScrcpyWindow";

    if (!RegisterClassEx(&wc)) {
        log_error("Failed to register window class");
        return false;
    }

    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    win->hwnd = CreateWindowEx(
        0,
        L"AutoScrcpyWindow",
        L"AutoScrcpy",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);

    if (!win->hwnd) {
        log_error("Failed to create window");
        return false;
    }

    return true;
}

void window_show(window_t *win) {
    ShowWindow(win->hwnd, SW_SHOW);
    UpdateWindow(win->hwnd);
}

void window_set_fullscreen(window_t *win, bool fullscreen) {
    if (fullscreen == win->fullscreen) return;

    win->fullscreen = fullscreen;

    if (fullscreen) {
        SetWindowLong(win->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(win->hwnd, HWND_TOP, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN),
                     GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED);
    } else {
        SetWindowLong(win->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(win->hwnd, NULL, 0, 0, win->width, win->height,
                     SWP_FRAMECHANGED);
    }
}

void window_set_always_on_top(window_t *win, bool always_on_top) {
    win->always_on_top = always_on_top;
    SetWindowPos(win->hwnd, always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void window_destroy(window_t *win) {
    if (win->hwnd) {
        DestroyWindow(win->hwnd);
    }
}
