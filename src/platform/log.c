#include "log.h"
#include <stdio.h>
#include <stdarg.h>

static enum log_level current_level = LOG_LEVEL_INFO;

void log_init(enum log_level level) {
    current_level = level;
}

void log_destroy(void) {
    /* nothing to clean up for now */
}

void log_error(const char *fmt, ...) {
    if (current_level > LOG_LEVEL_ERROR) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}
