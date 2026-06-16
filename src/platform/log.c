#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static enum log_level g_log_level = LOG_LEVEL_INFO;

void log_init(enum log_level level) {
    g_log_level = level;
}

void log_destroy(void) {
    // Nothing to clean up
}

void log_set_level(enum log_level level) {
    g_log_level = level;
}

void log_write(enum log_level level, const char *file, int line, const char *fmt, ...) {
    if (level < g_log_level) return;

    const char *level_str[] = {"VERBOSE", "DEBUG", "INFO", "WARN", "ERROR"};

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(stderr, "[%s] [%s] %s:%d: ", time_buf, level_str[level], file, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}
