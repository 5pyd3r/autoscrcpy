#ifndef SCRIPT_REPL_WINDOW_H
#define SCRIPT_REPL_WINDOW_H

#include <windows.h>
#include <commctrl.h>
#include <stdbool.h>
#include <stdint.h>

#define REPL_MAX_HISTORY 100
#define REPL_MAX_INPUT   4096
#define REPL_MAX_OUTPUT  (64 * 1024)

typedef void (*repl_eval_cb_t)(const char *code, void *userdata);

typedef struct {
    HWND hwnd;
    HWND h_output;   /* EDIT control - multiline readonly */
    HWND h_input;    /* EDIT control - single line */
    HFONT h_font;
    bool visible;
    bool initialized;

    char *history[REPL_MAX_HISTORY];
    int history_count;
    int history_pos;

    repl_eval_cb_t eval_cb;
    void *eval_userdata;

    char output_buf[REPL_MAX_OUTPUT];
    uint32_t output_len;
} repl_window_t;

bool repl_window_init(repl_window_t *win, HINSTANCE hInstance,
                      repl_eval_cb_t eval_cb, void *eval_userdata);
void repl_window_destroy(repl_window_t *win);
void repl_window_show(repl_window_t *win);
void repl_window_hide(repl_window_t *win);
void repl_window_toggle(repl_window_t *win);
bool repl_window_is_visible(const repl_window_t *win);
void repl_window_append_output(repl_window_t *win, const char *text);
void repl_window_append_error(repl_window_t *win, const char *text);
bool repl_window_process_message(repl_window_t *win, MSG *msg);

#endif /* SCRIPT_REPL_WINDOW_H */
