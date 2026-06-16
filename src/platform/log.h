#ifndef LOG_H
#define LOG_H

#include <stdint.h>

enum log_level {
    LOG_LEVEL_VERBOSE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

void log_init(enum log_level level);
void log_destroy(void);
void log_set_level(enum log_level level);

void log_write(enum log_level level, const char *file, int line, const char *fmt, ...);

#define log_verbose(...) log_write(LOG_LEVEL_VERBOSE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...)   log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)    log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)    log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...)   log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* LOG_H */
