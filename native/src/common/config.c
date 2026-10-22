#include "common/config.h"
#include "common/log.h"
#include "common/paths.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

void dots_config_init(struct dots_config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    if (dots_config_path(cfg->path, sizeof cfg->path) < 0)
        cfg->path[0] = '\0';
}

static int read_root(struct dots_config *cfg) {
    if (!cfg->path[0])
        return -1;

    FILE *f = fopen(cfg->path, "r");
    if (!f) {
        DOTS_WARN("config: cannot open %s: %s", cfg->path, strerror(errno));
        return -1;
    }
    if (fstat(fileno(f), &cfg->stamp) < 0) {
        fclose(f);
        return -1;
    }
    if (cfg->stamp.st_size <= 0 || cfg->stamp.st_size > 4 * 1024 * 1024) {
        fclose(f);
        DOTS_WARN("config: refused size %lld", (long long)cfg->stamp.st_size);
        return -1;
    }

    char *buf = malloc((size_t)cfg->stamp.st_size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)cfg->stamp.st_size, f);
    fclose(f);
    buf[got] = '\0';

    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        free(buf);
        return -1;
    }
    json_object *root = json_tokener_parse_ex(tok, buf, (int)got);
    enum json_tokener_error err = json_tokener_get_error(tok);
    json_tokener_free(tok);
    free(buf);

    if (err != json_tokener_success || !root) {
        DOTS_ERROR("config: parse error at %s", cfg->path);
        if (root)
            json_object_put(root);
        return -1;
    }
    if (!json_object_is_type(root, json_type_object)) {
        DOTS_ERROR("config: root is not an object");
        json_object_put(root);
        return -1;
    }

    if (cfg->root)
        json_object_put(cfg->root);
    cfg->root = root;
    cfg->loaded = 1;
    DOTS_INFO("config: loaded %s", cfg->path);
    return 0;
}

int dots_config_load(struct dots_config *cfg) {
    int rc = read_root(cfg);
    if (rc < 0) {
        cfg->root = json_object_new_object();
        cfg->loaded = cfg->root != NULL;
        return -1;
    }
    return 0;
}

int dots_config_reload_if_changed(struct dots_config *cfg) {
    struct stat st;
    if (!cfg->path[0] || stat(cfg->path, &st) < 0)
        return 0;
    if (cfg->loaded && st.st_mtim.tv_sec == cfg->stamp.st_mtim.tv_sec &&
        st.st_mtim.tv_nsec == cfg->stamp.st_mtim.tv_nsec &&
        st.st_size == cfg->stamp.st_size)
        return 0;
    return read_root(cfg);
}

void dots_config_destroy(struct dots_config *cfg) {
    if (cfg->root)
        json_object_put(cfg->root);
    memset(cfg, 0, sizeof *cfg);
}

json_object *dots_config_get(const struct dots_config *cfg, const char *path) {
    if (!cfg || !cfg->root || !path || !path[0])
        return NULL;
    char key[256];
    int n = snprintf(key, sizeof key, "%s", path);
    if (n < 0 || (size_t)n >= sizeof key)
        return NULL;

    json_object *cur = cfg->root;
    char *save = NULL;
    for (char *part = strtok_r(key, ".", &save); part;
         part = strtok_r(NULL, ".", &save)) {
        json_object *next = NULL;
        if (!json_object_object_get_ex(cur, part, &next))
            return NULL;
        cur = next;
    }
    return cur;
}

int dots_config_bool(const struct dots_config *cfg, const char *path, int fallback) {
    json_object *v = dots_config_get(cfg, path);
    if (!v || !json_object_is_type(v, json_type_boolean))
        return fallback;
    return json_object_get_boolean(v);
}

int dots_config_int(const struct dots_config *cfg, const char *path, int fallback) {
    json_object *v = dots_config_get(cfg, path);
    if (!v || !json_object_is_type(v, json_type_int))
        return fallback;
    return json_object_get_int(v);
}

double dots_config_double(const struct dots_config *cfg, const char *path, double fallback) {
    json_object *v = dots_config_get(cfg, path);
    if (!v)
        return fallback;
    if (json_object_is_type(v, json_type_double))
        return json_object_get_double(v);
    if (json_object_is_type(v, json_type_int))
        return (double)json_object_get_int(v);
    return fallback;
}

const char *dots_config_string(const struct dots_config *cfg, const char *path,
                               const char *fallback) {
    json_object *v = dots_config_get(cfg, path);
    if (!v || !json_object_is_type(v, json_type_string))
        return fallback;
    return json_object_get_string(v);
}
