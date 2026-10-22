#ifndef DOTS_LOG_H
#define DOTS_LOG_H

#include <stdarg.h>

enum dots_log_level {
    DOTS_LOG_ERROR = 0,
    DOTS_LOG_WARN = 1,
    DOTS_LOG_INFO = 2,
    DOTS_LOG_DEBUG = 3,
};

void dots_log_set_level(enum dots_log_level level);
void dots_log_init_from_env(void);
void dots_log(enum dots_log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define DOTS_ERROR(...) dots_log(DOTS_LOG_ERROR, __VA_ARGS__)
#define DOTS_WARN(...)  dots_log(DOTS_LOG_WARN, __VA_ARGS__)
#define DOTS_INFO(...)  dots_log(DOTS_LOG_INFO, __VA_ARGS__)
#define DOTS_DEBUG(...) dots_log(DOTS_LOG_DEBUG, __VA_ARGS__)

#endif
