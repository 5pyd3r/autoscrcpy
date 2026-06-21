#include "repl_window.h"
#include "platform/log.h"
#include <string.h>
#include <stdlib.h>

#define REPL_CLASS_NAME   "AutoScrcpyRepl"
#define REPL_WINDOW_TITLE "AutoScrcpy Scheme REPL"
#define REPL_MARGIN        5
#define REPL_INPUT_HEIGHT  24
#define WM_REPL_EVAL      (WM_USER + 1)

#define IDC_OUTPUT_EDIT   1001
#define IDC_INPUT_EDIT    1002

static void repl_execute_input(repl_window_t *win);

static LRESULT CALLBACK
repl_input_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                         UINT_PTR uidSubclass, DWORD_PTR dwRefData)
{
    (void)uidSubclass;
    repl_window_t *win = (repl_window_t *)dwRefData;

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            SendMessage(win->hwnd, WM_REPL_EVAL, 0, 0);
            return 0;
        }
        if (wParam == VK_UP) {
            if (win->history_count > 0) {
                int new_pos = win->history_pos - 1;
                if (new_pos < 0)
                    new_pos = win->history_count - 1;
                win->history_pos = new_pos;
                SetWindowTextA(win->h_input, win->history[win->history_pos]);
                SendMessage(win->h_input, EM_SETSEL, -1, -1);
            }
            return 0;
        }
        if (wParam == VK_DOWN) {
            if (win->history_count > 0) {
                int new_pos = win->history_pos + 1;
                if (new_pos >= win->history_count)
                    new_pos = 0;
                win->history_pos = new_pos;
                SetWindowTextA(win->h_input, win->history[win->history_pos]);
                SendMessage(win->h_input, EM_SETSEL, -1, -1);
            }
            return 0;
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, repl_input_subclass_proc, uidSubclass);
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK
repl_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    repl_window_t *win = (repl_window_t *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_SIZE:
        if (win && win->initialized) {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            int output_height = height - REPL_INPUT_HEIGHT - REPL_MARGIN * 3;

            MoveWindow(win->h_output, REPL_MARGIN, REPL_MARGIN,
                       width - REPL_MARGIN * 2, output_height, TRUE);
            MoveWindow(win->h_input, REPL_MARGIN,
                       output_height + REPL_MARGIN * 2,
                       width - REPL_MARGIN * 2, REPL_INPUT_HEIGHT, TRUE);
        }
        return 0;

    case WM_REPL_EVAL:
        if (win)
            repl_execute_input(win);
        return 0;

    case WM_CLOSE:
        if (win) {
            repl_window_hide(win);
            return 0;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void
repl_execute_input(repl_window_t *win)
{
    char buf[REPL_MAX_INPUT];
    int len = GetWindowTextA(win->h_input, buf, sizeof(buf));
    if (len <= 0)
        return;

    /* Add to history */
    if (win->history_count < REPL_MAX_HISTORY) {
        win->history[win->history_count] = _strdup(buf);
        if (!win->history[win->history_count]) {
            log_error("repl: failed to duplicate history entry");
            return;
        }
        win->history_count++;
    } else {
        /* Shift history */
        free(win->history[0]);
        memmove(&win->history[0], &win->history[1],
                sizeof(char *) * (REPL_MAX_HISTORY - 1));
        win->history[REPL_MAX_HISTORY - 1] = _strdup(buf);
        if (!win->history[REPL_MAX_HISTORY - 1]) {
            log_error("repl: failed to duplicate history entry");
            return;
        }
    }
    win->history_pos = win->history_count;

    /* Echo to output */
    repl_window_append_output(win, "> ");
    repl_window_append_output(win, buf);
    repl_window_append_output(win, "\n");

    /* Clear input */
    SetWindowTextA(win->h_input, "");

    /* Call eval callback */
    if (win->eval_cb)
        win->eval_cb(buf, win->eval_userdata);
}

bool
repl_window_init(repl_window_t *win, HINSTANCE hInstance,
                 repl_eval_cb_t eval_cb, void *eval_userdata)
{
    memset(win, 0, sizeof(*win));
    win->eval_cb = eval_cb;
    win->eval_userdata = eval_userdata;

    /* Register window class */
    WNDCLASSEXA wc = {
        .cbSize        = sizeof(WNDCLASSEXA),
        .style         = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc   = repl_wnd_proc,
        .hInstance     = hInstance,
        .hCursor       = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
        .lpszClassName = REPL_CLASS_NAME,
    };

    if (!RegisterClassExA(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log_error("repl: RegisterClassEx failed: %lu", err);
            return false;
        }
    }

    /* Create main window */
    win->hwnd = CreateWindowExA(
        0, REPL_CLASS_NAME, REPL_WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        NULL, NULL, hInstance, NULL);

    if (!win->hwnd) {
        log_error("repl: CreateWindowEx failed: %lu", GetLastError());
        return false;
    }

    /* Store pointer for WndProc */
    SetWindowLongPtrA(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    /* Create monospace font */
    win->h_font = CreateFontA(
        14, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    if (!win->h_font) {
        log_error("repl: CreateFont failed: %lu", GetLastError());
        DestroyWindow(win->hwnd);
        return false;
    }

    /* Create output EDIT control */
    win->h_output = CreateWindowExA(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        REPL_MARGIN, REPL_MARGIN, 580, 330,
        win->hwnd, (HMENU)(UINT_PTR)IDC_OUTPUT_EDIT, hInstance, NULL);

    if (!win->h_output) {
        log_error("repl: CreateWindow output failed: %lu", GetLastError());
        DeleteObject(win->h_font);
        DestroyWindow(win->hwnd);
        return false;
    }

    /* Create input EDIT control */
    win->h_input = CreateWindowExA(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        REPL_MARGIN, 340, 580, REPL_INPUT_HEIGHT,
        win->hwnd, (HMENU)(UINT_PTR)IDC_INPUT_EDIT, hInstance, NULL);

    if (!win->h_input) {
        log_error("repl: CreateWindow input failed: %lu", GetLastError());
        DestroyWindow(win->h_output);
        DeleteObject(win->h_font);
        DestroyWindow(win->hwnd);
        return false;
    }

    /* Set font for both controls */
    SendMessage(win->h_output, WM_SETFONT, (WPARAM)win->h_font, TRUE);
    SendMessage(win->h_input, WM_SETFONT, (WPARAM)win->h_font, TRUE);

    /* Subclass input control */
    if (!SetWindowSubclass(win->h_input, repl_input_subclass_proc,
                           IDC_INPUT_EDIT, (DWORD_PTR)win)) {
        log_error("repl: SetWindowSubclass failed: %lu", GetLastError());
        DestroyWindow(win->h_input);
        DestroyWindow(win->h_output);
        DeleteObject(win->h_font);
        DestroyWindow(win->hwnd);
        return false;
    }

    win->initialized = true;
    log_info("repl: window initialized");
    return true;
}

void
repl_window_destroy(repl_window_t *win)
{
    if (!win->initialized)
        return;

    /* Free history */
    for (int i = 0; i < win->history_count; i++) {
        free(win->history[i]);
        win->history[i] = NULL;
    }
    win->history_count = 0;

    /* Destroy controls and window */
    if (win->h_input) {
        DestroyWindow(win->h_input);
        win->h_input = NULL;
    }
    if (win->h_output) {
        DestroyWindow(win->h_output);
        win->h_output = NULL;
    }
    if (win->h_font) {
        DeleteObject(win->h_font);
        win->h_font = NULL;
    }
    if (win->hwnd) {
        DestroyWindow(win->hwnd);
        win->hwnd = NULL;
    }

    UnregisterClassA(REPL_CLASS_NAME, GetModuleHandle(NULL));
    win->initialized = false;
    log_info("repl: window destroyed");
}

void
repl_window_show(repl_window_t *win)
{
    if (!win->initialized)
        return;

    ShowWindow(win->hwnd, SW_SHOW);
    UpdateWindow(win->hwnd);
    SetFocus(win->h_input);
    win->visible = true;
}

void
repl_window_hide(repl_window_t *win)
{
    if (!win->initialized)
        return;

    ShowWindow(win->hwnd, SW_HIDE);
    win->visible = false;
}

void
repl_window_toggle(repl_window_t *win)
{
    if (win->visible)
        repl_window_hide(win);
    else
        repl_window_show(win);
}

bool
repl_window_is_visible(const repl_window_t *win)
{
    return win->visible;
}

void
repl_window_append_output(repl_window_t *win, const char *text)
{
    if (!win->initialized || !text)
        return;

    /* Move caret to end */
    int len = GetWindowTextLengthA(win->h_output);
    SendMessage(win->h_output, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    /* Append text */
    SendMessage(win->h_output, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void
repl_window_append_error(repl_window_t *win, const char *text)
{
    if (!win->initialized || !text)
        return;

    repl_window_append_output(win, "*** Error: ");
    repl_window_append_output(win, text);
    repl_window_append_output(win, "\n");
}

bool
repl_window_process_message(repl_window_t *win, MSG *msg)
{
    if (!win->initialized || !win->visible)
        return false;

    if (IsDialogMessageA(win->hwnd, msg))
        return true;

    return false;
}
