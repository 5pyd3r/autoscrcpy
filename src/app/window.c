#include "window.h"
#include "../platform/log.h"
#include <string.h>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* Handle WM_NCCREATE to set GWLP_USERDATA before any other messages */
    if (msg == WM_NCCREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    window_t *win = (window_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (win && win->callbacks.key_cb)
                win->callbacks.key_cb((uint32_t)wParam, true, win->callbacks.userdata);
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (win && win->callbacks.key_cb)
                win->callbacks.key_cb((uint32_t)wParam, false, win->callbacks.userdata);
            return 0;

        case WM_LBUTTONDOWN:
            if (win && win->callbacks.mouse_cb)
                win->callbacks.mouse_cb((int16_t)LOWORD(lParam), (int16_t)HIWORD(lParam),
                                         1, 1, win->callbacks.userdata);
            return 0;
        case WM_LBUTTONUP:
            if (win && win->callbacks.mouse_cb)
                win->callbacks.mouse_cb((int16_t)LOWORD(lParam), (int16_t)HIWORD(lParam),
                                         1, 0, win->callbacks.userdata);
            return 0;
        case WM_RBUTTONDOWN:
            if (win && win->callbacks.mouse_cb)
                win->callbacks.mouse_cb((int16_t)LOWORD(lParam), (int16_t)HIWORD(lParam),
                                         2, 1, win->callbacks.userdata);
            return 0;
        case WM_RBUTTONUP:
            if (win && win->callbacks.mouse_cb)
                win->callbacks.mouse_cb((int16_t)LOWORD(lParam), (int16_t)HIWORD(lParam),
                                         2, 0, win->callbacks.userdata);
            return 0;
        case WM_MOUSEMOVE: {
            if (win && win->callbacks.mouse_cb) {
                uint32_t buttons = 0;
                if (wParam & MK_LBUTTON) buttons |= 1;
                if (wParam & MK_RBUTTON) buttons |= 2;
                win->callbacks.mouse_cb((int16_t)LOWORD(lParam), (int16_t)HIWORD(lParam),
                                         buttons, 2, win->callbacks.userdata);
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            if (win && win->callbacks.wheel_cb)
                win->callbacks.wheel_cb((int16_t)LOWORD(lParam), (int16_t)HIWORD(lParam),
                                         GET_WHEEL_DELTA_WPARAM(wParam), win->callbacks.userdata);
            return 0;

        case WM_SIZE:
            if (win && win->callbacks.resize_cb) {
                win->width = (int32_t)LOWORD(lParam);
                win->height = (int32_t)HIWORD(lParam);
                win->callbacks.resize_cb(win->width, win->height, win->callbacks.userdata);
            }
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
    memset(&win->callbacks, 0, sizeof(win->callbacks));

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
        0, L"AutoScrcpyWindow", L"AutoScrcpy",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, win);

    if (!win->hwnd) {
        log_error("Failed to create window");
        return false;
    }

    SetWindowLongPtr(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);
    return true;
}

void window_set_callbacks(window_t *win, const window_callbacks_t *callbacks) {
    win->callbacks = *callbacks;
}

void window_show(window_t *win) {
    ShowWindow(win->hwnd, SW_SHOW);
    UpdateWindow(win->hwnd);
    /* Bring window to foreground */
    SetForegroundWindow(win->hwnd);
    SetFocus(win->hwnd);
}

void window_set_fullscreen(window_t *win, bool fullscreen) {
    if (fullscreen == win->fullscreen) return;
    win->fullscreen = fullscreen;

    if (fullscreen) {
        SetWindowLong(win->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(win->hwnd, HWND_TOP, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED);
    } else {
        SetWindowLong(win->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(win->hwnd, NULL, 0, 0, win->width, win->height, SWP_FRAMECHANGED);
    }
}

void window_set_always_on_top(window_t *win, bool always_on_top) {
    win->always_on_top = always_on_top;
    SetWindowPos(win->hwnd, always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void window_destroy(window_t *win) {
    if (win->hwnd) DestroyWindow(win->hwnd);
}
