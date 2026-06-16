#include "clipboard.h"
#include "../platform/log.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

bool clipboard_init(void) {
    return true;
}

bool clipboard_get_text(char **text, uint32_t *len) {
    if (!OpenClipboard(NULL)) {
        log_error("Failed to open clipboard");
        return false;
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return false;
    }

    wchar_t *wtext = GlobalLock(hData);
    if (!wtext) {
        CloseClipboard();
        return false;
    }

    // Convert to UTF-8
    int wlen = wcslen(wtext);
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, NULL, 0, NULL, NULL);
    *text = malloc(utf8_len + 1);
    if (!*text) {
        GlobalUnlock(hData);
        CloseClipboard();
        return false;
    }

    WideCharToMultiByte(CP_UTF8, 0, wtext, wlen, *text, utf8_len, NULL, NULL);
    (*text)[utf8_len] = '\0';
    *len = utf8_len;

    GlobalUnlock(hData);
    CloseClipboard();

    return true;
}

bool clipboard_set_text(const char *text, uint32_t len) {
    if (!OpenClipboard(NULL)) {
        log_error("Failed to open clipboard");
        return false;
    }

    EmptyClipboard();

    // Convert to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
    if (!hData) {
        CloseClipboard();
        return false;
    }

    wchar_t *wtext = GlobalLock(hData);
    MultiByteToWideChar(CP_UTF8, 0, text, len, wtext, wlen);
    wtext[wlen] = '\0';
    GlobalUnlock(hData);

    SetClipboardData(CF_UNICODETEXT, hData);
    CloseClipboard();

    return true;
}

void clipboard_destroy(void) {
    // Nothing to clean up
}
#else
// POSIX stub implementation
bool clipboard_init(void) { return true; }
bool clipboard_get_text(char **text, uint32_t *len) {
    (void)text;
    (void)len;
    return false;
}
bool clipboard_set_text(const char *text, uint32_t len) {
    (void)text;
    (void)len;
    return false;
}
void clipboard_destroy(void) {}
#endif
