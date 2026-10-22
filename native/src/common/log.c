#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static enum dots_log_level current_level = DOTS_LOG_INFO;

void dots_log_set_level(enum dots_log_level level) {
    current_level = level;
}

static const char *level_name(enum dots_log_level level) {
    switch (level) {
    case DOTS_LOG_ERROR: return "error";
    case DOTS_LOG_WARN:  return "warn";
    case DOTS_LOG_INFO:  return "info";
    case DOTS_LOG_DEBUG: return "debug";
    }
    return "?";
}

void dots_log(enum dots_log_level level, const char *fmt, ...) {
    if (level > current_level)
        return;

    char stamp[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof stamp, "%H:%M:%S", &tm);

    fprintf(stderr, "[%s] %-5s ", stamp, level_name(level));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void dots_log_init_from_env(void);
void dots_log_init_from_env(void) {
    const char *env = getenv("DOTS_LOG_LEVEL");
    if (!env || !env[0])
        return;
    if (strcmp(env, "error") == 0)
        current_level = DOTS_LOG_ERROR;
    else if (strcmp(env, "warn") == 0)
        current_level = DOTS_LOG_WARN;
    else if (strcmp(env, "info") == 0)
        current_level = DOTS_LOG_INFO;
    else if (strcmp(env, "debug") == 0)
        current_level = DOTS_LOG_DEBUG;
}
