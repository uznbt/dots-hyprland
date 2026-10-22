#ifndef DOTS_CONFIG_H
#define DOTS_CONFIG_H

#include <stddef.h>
#include <sys/stat.h>
#include <json-c/json.h>

struct dots_config {
    char path[1024];
    json_object *root;
    struct stat stamp;
    int loaded;
};

void dots_config_init(struct dots_config *cfg);
int dots_config_load(struct dots_config *cfg);
int dots_config_reload_if_changed(struct dots_config *cfg);
void dots_config_destroy(struct dots_config *cfg);

/* Path bertitik, mis. "bar.resources.showMemory". */
json_object *dots_config_get(const struct dots_config *cfg, const char *path);
int dots_config_bool(const struct dots_config *cfg, const char *path, int fallback);
int dots_config_int(const struct dots_config *cfg, const char *path, int fallback);
double dots_config_double(const struct dots_config *cfg, const char *path, double fallback);
const char *dots_config_string(const struct dots_config *cfg, const char *path,
                               const char *fallback);

#endif
