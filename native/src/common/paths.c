#include "common/paths.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_join(char *buf, size_t size, const char *a, const char *b) {
    int n = snprintf(buf, size, "%s/%s", a, b);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static const char *home_dir(void) {
    const char *home = getenv("HOME");
    return (home && home[0]) ? home : "/tmp";
}

static const char *runtime_base(void) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    return (runtime && runtime[0]) ? runtime : "/tmp";
}

int dots_runtime_dir(char *buf, size_t size) {
    return path_join(buf, size, runtime_base(), "dots");
}

int dots_socket_path(char *buf, size_t size) {
    char dir[512];
    if (dots_runtime_dir(dir, sizeof dir) < 0)
        return -1;
    return path_join(buf, size, dir, "daemon.sock");
}

int dots_pid_path(char *buf, size_t size) {
    char dir[512];
    if (dots_runtime_dir(dir, sizeof dir) < 0)
        return -1;
    return path_join(buf, size, dir, "dotsd.pid");
}

int dots_config_path(char *buf, size_t size) {
    const char *config = getenv("XDG_CONFIG_HOME");
    char dir[512];
    if (config && config[0]) {
        if (path_join(dir, sizeof dir, config, "illogical-impulse") < 0)
            return -1;
    } else {
        char base[512];
        int n = snprintf(base, sizeof base, "%s/.config", home_dir());
        if (n < 0 || (size_t)n >= sizeof base)
            return -1;
        if (path_join(dir, sizeof dir, base, "illogical-impulse") < 0)
            return -1;
    }
    return path_join(buf, size, dir, "config.json");
}

int dots_state_dir(char *buf, size_t size) {
    const char *state = getenv("XDG_STATE_HOME");
    char base[512];
    if (state && state[0]) {
        if (path_join(base, sizeof base, state, "dots") < 0)
            return -1;
    } else {
        char home[512];
        int n = snprintf(home, sizeof home, "%s/.local/state", home_dir());
        if (n < 0 || (size_t)n >= sizeof home)
            return -1;
        if (path_join(base, sizeof base, home, "dots") < 0)
            return -1;
    }
    int n = snprintf(buf, size, "%s", base);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int dots_cache_dir(char *buf, size_t size) {
    const char *cache = getenv("XDG_CACHE_HOME");
    char base[512];
    if (cache && cache[0]) {
        if (path_join(base, sizeof base, cache, "dots") < 0)
            return -1;
    } else {
        char home[512];
        int n = snprintf(home, sizeof home, "%s/.cache", home_dir());
        if (n < 0 || (size_t)n >= sizeof home)
            return -1;
        if (path_join(base, sizeof base, home, "dots") < 0)
            return -1;
    }
    int n = snprintf(buf, size, "%s", base);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int dots_mkdir_p(const char *path, unsigned int mode) {
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    char tmp[1024];
    int n = snprintf(tmp, sizeof tmp, "%s", path);
    if (n < 0 || (size_t)n >= sizeof tmp) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, mode) < 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
    return 0;
}
