#ifndef PLATFORM_LOG_H
#define PLATFORM_LOG_H

enum log_level {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

void log_init(enum log_level level);
void log_destroy(void);
void log_error(const char *fmt, ...);

#endif /* PLATFORM_LOG_H */
