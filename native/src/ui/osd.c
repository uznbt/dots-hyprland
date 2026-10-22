// dots-osd - OSD volume/brightness Wayland native (C + cairo/pango).
//
// Proses ini hanya hidup selama OSD terlihat. Invocation berikutnya mengirim
// command lewat Unix datagram socket ke instance yang sedang berjalan, jadi
// tidak ada proses idle dan tidak ada penambahan PSS permanen.

#define _GNU_SOURCE
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

#include "common/config.h"
#include "common/log.h"
#include "common/paths.h"

#include "wlr-layer-shell-client.h"

#define OSD_SURFACE_H 72
#define OSD_CARD_W 200
#define OSD_CARD_H 64
#define OSD_RADIUS 22
#define FADE_IN_MS 120
#define FADE_OUT_MS 140
#define OSD_DEFAULT_TIMEOUT_MS 1000
#define OSD_MAX_OUTPUTS 16
#define OSD_SOCKET_NAME "osd.sock"

struct rgba { double r, g, b, a; };
static const struct rgba C_LAYER0       = { 18/255.0, 23/255.0, 20/255.0, 0.97 };
static const struct rgba C_LAYER1       = { 86/255.0, 83/255.0, 90/255.0, 0.18 };
static const struct rgba C_PRIMARY      = { 135/255.0, 214/255.0, 189/255.0, 1.0 };
static const struct rgba C_ON_LAYER0    = { 222/255.0, 228/255.0, 224/255.0, 1.0 };
static const struct rgba C_ON_LAYER1    = { 191/255.0, 201/255.0, 196/255.0, 1.0 };
static const struct rgba C_SUBTEXT      = { 137/255.0, 147/255.0, 142/255.0, 1.0 };
static const struct rgba C_BORDER       = { 48/255.0, 58/255.0, 53/255.0, 1.0 };

#define VAR_MAIN  "wght=450,wdth=100,opsz=100"

enum osd_kind {
    OSD_VOLUME = 0,
    OSD_BRIGHTNESS = 1,
};

struct osd_output {
    struct wl_output *wl;
    uint32_t global_name;
    char name[64];
    int width, height;
};

struct osd {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct osd_output outputs[OSD_MAX_OUTPUTS];
    int n_outputs;
    struct osd_output *target;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer *buffer;
    void *data;
    int buf_w, buf_h;
    int configured;

    int bar_bottom;
    int corner_style;
    int timeout_ms;

    enum osd_kind kind;
    double value;
    int muted;
    int visible;
    long long shown_ms;
    long long hide_at;

    int redraw;
    volatile sig_atomic_t running;
    int sock_fd;
    char sock_path[512];
    int sock_owned;
    int qs_suspended;
};

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---------------- command execution ---------------- */

static int run_capture(char *const argv[], char *out, size_t cap) {
    int p[2];
    if (pipe(p) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(p[1]);
    size_t off = 0;
    char sink[256];
    char *dst = out;
    size_t dst_cap = cap;
    if (!dst || dst_cap == 0) {
        dst = sink;
        dst_cap = sizeof sink;
        dst[0] = 0;
    }
    ssize_t n;
    while ((n = read(p[0], dst + off, dst_cap - 1 - off)) > 0) {
        off += (size_t)n;
        if (off >= dst_cap - 1) {
            // Drain the rest without storing it.
            while (read(p[0], sink, sizeof sink) > 0) {
            }
            break;
        }
    }
    dst[off] = 0;
    close(p[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static void qs_osd_suspend(int suspend) {
    // Jembatan transisi: Quickshell tetap menjadi fallback, tetapi kartu OSD
    // QML harus berhenti tampil selama OSD native aktif. Kalau Quickshell
    // tidak berjalan, command ini gagal dengan cepat dan tidak berbahaya.
    const char *config = getenv("DOTS_QS_CONFIG");
    if (!config || !config[0])
        config = "ii";
    char *argv[] = { "qs", "-c", (char *)config, "ipc", "call",
                     "osdVolume", suspend ? "nativeOn" : "nativeOff", NULL };
    (void)run_capture(argv, NULL, 0);
}

static int read_volume(double *value, int *muted) {
    char out[256] = "";
    char *argv[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
    if (run_capture(argv, out, sizeof out) < 0)
        return -1;
    const char *p = strstr(out, "Volume:");
    if (!p)
        return -1;
    double v = 0;
    if (sscanf(p, "Volume: %lf", &v) != 1)
        return -1;
    if (v < 0)
        v = 0;
    if (value)
        *value = v;
    if (muted)
        *muted = strstr(out, "[MUTED]") != NULL;
    return 0;
}

static int read_brightness(double *value) {
    char out[256] = "";
    char *argv[] = { "brightnessctl", "-m", NULL };
    if (run_capture(argv, out, sizeof out) < 0)
        return -1;

    // Format: device,class,current,percent,max
    char copy[256];
    snprintf(copy, sizeof copy, "%s", out);
    char *save = NULL;
    int field = 0;
    long current = -1, maximum = -1;
    for (char *tok = strtok_r(copy, ",\n", &save); tok;
         tok = strtok_r(NULL, ",\n", &save), field++) {
        if (field == 2)
            current = strtol(tok, NULL, 10);
        else if (field == 4)
            maximum = strtol(tok, NULL, 10);
    }
    if (current >= 0 && maximum > 0) {
        *value = (double)current / (double)maximum;
        return 0;
    }

    // Fallback for brightnessctl versions that omit -m.
    char cur[64] = "", max[64] = "";
    char *gargv[] = { "brightnessctl", "g", NULL };
    char *margv[] = { "brightnessctl", "m", NULL };
    if (run_capture(gargv, cur, sizeof cur) < 0 ||
        run_capture(margv, max, sizeof max) < 0)
        return -1;
    long c = strtol(cur, NULL, 10);
    long m = strtol(max, NULL, 10);
    if (m <= 0)
        return -1;
    *value = (double)c / (double)m;
    return 0;
}

static void apply_volume(const char *action) {
    char *up[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                   "2%+", "-l", "1.5", NULL };
    char *down[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                     "2%-", NULL };
    char *mute[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@",
                     "toggle", NULL };
    if (strcmp(action, "up") == 0)
        (void)run_capture(up, NULL, 0);
    else if (strcmp(action, "down") == 0)
        (void)run_capture(down, NULL, 0);
    else if (strcmp(action, "mute") == 0)
        (void)run_capture(mute, NULL, 0);
}

static void apply_brightness(const char *action) {
    char *up[] = { "brightnessctl", "--class", "backlight", "s", "5%+",
                   "--quiet", NULL };
    char *down[] = { "brightnessctl", "--class", "backlight", "s", "5%-",
                     "--quiet", NULL };
    char *up_fallback[] = { "brightnessctl", "s", "5%+", "--quiet", NULL };
    char *down_fallback[] = { "brightnessctl", "s", "5%-", "--quiet", NULL };
    if (strcmp(action, "up") == 0) {
        if (run_capture(up, NULL, 0) < 0)
            (void)run_capture(up_fallback, NULL, 0);
    } else if (strcmp(action, "down") == 0) {
        if (run_capture(down, NULL, 0) < 0)
            (void)run_capture(down_fallback, NULL, 0);
    }
}

/* ---------------- config / socket ---------------- */

static int socket_path(char *buf, size_t size) {
    const char *fixed = getenv("DOTS_OSD_SOCKET");
    if (fixed && fixed[0]) {
        int n = snprintf(buf, size, "%s", fixed);
        return (n < 0 || (size_t)n >= size) ? -1 : 0;
    }
    char dir[512];
    if (dots_runtime_dir(dir, sizeof dir) < 0)
        return -1;
    if (dots_mkdir_p(dir, 0700) < 0)
        return -1;
    int n = snprintf(buf, size, "%s/%s", dir, OSD_SOCKET_NAME);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static int make_sockaddr(struct sockaddr_un *addr, const char *path) {
    memset(addr, 0, sizeof *addr);
    addr->sun_family = AF_UNIX;
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof addr->sun_path)
        return -1;
    memcpy(addr->sun_path, path, len + 1);
    return 0;
}

static int send_existing(const char *path, const char *msg) {
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    int rc = -1;
    if (make_sockaddr(&addr, path) == 0 &&
        connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0 &&
        send(fd, msg, strlen(msg), 0) >= 0)
        rc = 0;
    close(fd);
    return rc;
}

static int bind_socket(const char *path, int *out_fd) {
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    if (make_sockaddr(&addr, path) < 0) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    *out_fd = fd;
    return 0;
}

static void load_config(struct osd *o, struct dots_config *cfg) {
    o->bar_bottom = 0;
    o->corner_style = 0;
    o->timeout_ms = OSD_DEFAULT_TIMEOUT_MS;

    dots_config_init(cfg);
    if (dots_config_load(cfg) == 0) {
        o->bar_bottom = dots_config_bool(cfg, "bar.bottom", 0);
        o->corner_style = dots_config_int(cfg, "bar.cornerStyle", 0);
        int t = dots_config_int(cfg, "osd.timeout", OSD_DEFAULT_TIMEOUT_MS);
        if (t > 0)
            o->timeout_ms = t;
    }
    const char *env = getenv("DOTS_OSD_TIMEOUT_MS");
    if (env && env[0]) {
        int t = atoi(env);
        if (t > 0)
            o->timeout_ms = t;
    }
    if (o->timeout_ms < 200)
        o->timeout_ms = 200;
    if (o->timeout_ms > 60000)
        o->timeout_ms = 60000;
}

static int bar_height(const struct osd *o) {
    // Appearance.sizes.barHeight: 40, or 50 for cornerStyle 1.
    return 40 + (o->corner_style == 1 ? 10 : 0);
}

/* ---------------- Wayland output selection ---------------- */

static void output_geometry(void *data, struct wl_output *wl, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t subpixel,
                            const char *make, const char *model, int32_t transform) {
    (void)data; (void)wl; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *wl, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    (void)wl; (void)flags; (void)refresh;
    struct osd_output *out = data;
    out->width = width;
    out->height = height;
}

static void output_name(void *data, struct wl_output *wl, const char *name) {
    (void)wl;
    struct osd_output *out = data;
    snprintf(out->name, sizeof out->name, "%s", name ? name : "");
}

static void output_description(void *data, struct wl_output *wl,
                               const char *description) {
    (void)data; (void)wl; (void)description;
}

static void output_scale(void *data, struct wl_output *wl, int32_t factor) {
    (void)data; (void)wl; (void)factor;
}

static void output_done(void *data, struct wl_output *wl) {
    (void)data; (void)wl;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .name = output_name,
    .done = output_done,
    .scale = output_scale,
    .description = output_description,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *iface, uint32_t version) {
    struct osd *o = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        o->compositor = wl_registry_bind(registry, name,
                                         &wl_compositor_interface,
                                         version < 4 ? version : 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        o->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        o->layer_shell = wl_registry_bind(registry, name,
                                          &zwlr_layer_shell_v1_interface,
                                          version < 4 ? version : 4);
    } else if (strcmp(iface, wl_output_interface.name) == 0 &&
               o->n_outputs < OSD_MAX_OUTPUTS) {
        struct osd_output *out = &o->outputs[o->n_outputs++];
        memset(out, 0, sizeof *out);
        out->global_name = name;
        out->wl = wl_registry_bind(registry, name, &wl_output_interface,
                                   version < 4 ? version : 4);
        if (out->wl)
            wl_output_add_listener(out->wl, &output_listener, out);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
    (void)registry;
    struct osd *o = data;
    for (int i = 0; i < o->n_outputs; i++) {
        if (o->outputs[i].global_name != name)
            continue;
        char target_name[64] = "";
        int had_target = o->target != NULL;
        if (had_target)
            snprintf(target_name, sizeof target_name, "%s", o->target->name);
        if (o->outputs[i].wl)
            wl_output_destroy(o->outputs[i].wl);
        memmove(&o->outputs[i], &o->outputs[i + 1],
                (size_t)(o->n_outputs - i - 1) * sizeof o->outputs[0]);
        o->n_outputs--;
        o->target = NULL;
        if (had_target) {
            for (int j = 0; j < o->n_outputs; j++) {
                if (strcmp(o->outputs[j].name, target_name) == 0) {
                    o->target = &o->outputs[j];
                    break;
                }
            }
        }
        return;
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void focused_output_name(char *out, size_t size) {
    out[0] = 0;
    const char *fixed = getenv("DOTS_OUTPUT");
    if (fixed && fixed[0]) {
        snprintf(out, size, "%s", fixed);
        return;
    }

    char json[64 * 1024] = "";
    char *argv[] = { "hyprctl", "monitors", "-j", NULL };
    if (run_capture(argv, json, sizeof json) < 0)
        return;

    json_object *root = json_tokener_parse(json);
    if (!root || !json_object_is_type(root, json_type_array)) {
        if (root)
            json_object_put(root);
        return;
    }
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; i++) {
        json_object *mon = json_object_array_get_idx(root, i);
        json_object *focused = NULL, *name = NULL;
        if (!json_object_is_type(mon, json_type_object))
            continue;
        json_object_object_get_ex(mon, "focused", &focused);
        json_object_object_get_ex(mon, "name", &name);
        if (focused && json_object_get_boolean(focused) && name &&
            json_object_is_type(name, json_type_string)) {
            snprintf(out, size, "%s", json_object_get_string(name));
            break;
        }
    }
    json_object_put(root);
}

static struct osd_output *select_output(struct osd *o, const char *wanted) {
    if (wanted && wanted[0]) {
        for (int i = 0; i < o->n_outputs; i++) {
            if (o->outputs[i].wl &&
                strcmp(o->outputs[i].name, wanted) == 0)
                return &o->outputs[i];
        }
    }
    for (int i = 0; i < o->n_outputs; i++) {
        if (o->outputs[i].wl && o->outputs[i].name[0])
            return &o->outputs[i];
    }
    for (int i = 0; i < o->n_outputs; i++) {
        if (o->outputs[i].wl)
            return &o->outputs[i];
    }
    return NULL;
}

/* ---------------- rendering ---------------- */

static int create_shm_file(size_t size) {
    int fd = memfd_create("dots-osd", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }
    char name[64];
    snprintf(name, sizeof name, "/dots-osd-%d", getpid());
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;
    shm_unlink(name);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void create_buffer(struct osd *o, int w, int h) {
    if (o->buffer) {
        wl_buffer_destroy(o->buffer);
        o->buffer = NULL;
    }
    if (o->data) {
        munmap(o->data, (size_t)o->buf_w * (size_t)o->buf_h * 4);
        o->data = NULL;
    }
    if (w <= 0 || h <= 0)
        return;

    size_t size = (size_t)w * (size_t)h * 4;
    int fd = create_shm_file(size);
    if (fd < 0)
        return;
    o->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (o->data == MAP_FAILED) {
        o->data = NULL;
        close(fd);
        return;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(o->shm, fd, (int)size);
    o->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4,
                                          WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    o->buf_w = w;
    o->buf_h = h;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                         double r) {
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
}

static PangoLayout *make_layout(cairo_t *cr, const char *text, double px) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, "Google Sans Flex");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_font_description_set_variations(d, VAR_MAIN);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, text, -1);
    pango_layout_set_single_paragraph_mode(l, TRUE);
    return l;
}

static void draw_text_left(cairo_t *cr, const char *text, double px,
                           double x, double y, struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_right(cairo_t *cr, const char *text, double px,
                            double right, double y, struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, right - w, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static PangoLayout *make_icon(cairo_t *cr, const char *name, double px) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, "Material Symbols Rounded");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_font_description_set_weight(d, PANGO_WEIGHT_SEMIBOLD);
    char var[64];
    snprintf(var, sizeof var, "FILL=1,opsz=%.0f", px);
    pango_font_description_set_variations(d, var);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, name, -1);
    return l;
}

static void draw_icon_mid(cairo_t *cr, const char *name, double px,
                          double cx, double cy, struct rgba col) {
    PangoLayout *l = make_icon(cr, name, px);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static double opacity_at(const struct osd *o, long long now) {
    if (!o->visible)
        return 0;
    if (now < o->shown_ms)
        return 0;
    if (now < o->shown_ms + FADE_IN_MS)
        return (double)(now - o->shown_ms) / FADE_IN_MS;
    if (now < o->hide_at)
        return 1.0;
    if (now < o->hide_at + FADE_OUT_MS)
        return 1.0 - (double)(now - o->hide_at) / FADE_OUT_MS;
    return 0.0;
}

static void draw(struct osd *o) {
    if (!o->configured || !o->data || !o->buffer)
        return;

    long long now = mono_ms();
    double opacity = opacity_at(o, now);
    if (opacity < 0)
        opacity = 0;
    if (opacity > 1)
        opacity = 1;

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        o->data, CAIRO_FORMAT_ARGB32, o->buf_w, o->buf_h, o->buf_w * 4);
    cairo_t *cr = cairo_create(cs);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const double x = (o->buf_w - OSD_CARD_W) / 2.0;
    const double y = (OSD_SURFACE_H - OSD_CARD_H) / 2.0;

    cairo_push_group(cr);

    // Shadow.
    rounded_rect(cr, x + 1, y + 3, OSD_CARD_W, OSD_CARD_H, OSD_RADIUS);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.28);
    cairo_fill(cr);

    // Card.
    rounded_rect(cr, x, y, OSD_CARD_W, OSD_CARD_H, OSD_RADIUS);
    cairo_set_source_rgba(cr, C_LAYER0.r, C_LAYER0.g, C_LAYER0.b, C_LAYER0.a);
    cairo_fill(cr);

    rounded_rect(cr, x + 0.5, y + 0.5, OSD_CARD_W - 1, OSD_CARD_H - 1,
                 OSD_RADIUS - 0.5);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, C_BORDER.r, C_BORDER.g, C_BORDER.b, 1);
    cairo_stroke(cr);

    // Icon.
    const char *icon;
    if (o->kind == OSD_VOLUME)
        icon = o->muted ? "volume_off" : "volume_up";
    else
        icon = "light_mode";
    draw_icon_mid(cr, icon, 22, x + 27, y + 29, C_ON_LAYER0);

    // Label and value.
    const char *label = o->kind == OSD_VOLUME ? "Volume" : "Brightness";
    char value[16];
    int pct = (int)lround(o->value * 100);
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    snprintf(value, sizeof value, "%d%%", pct);

    draw_text_left(cr, label, 13, x + 49, y + 12, C_ON_LAYER0);
    draw_text_right(cr, value, 13, x + OSD_CARD_W - 16, y + 12, C_ON_LAYER1);

    // Progress bar.
    const double px = x + 49;
    const double pw = OSD_CARD_W - 49 - 16;
    const double py = y + 40;
    const double ph = 6;
    rounded_rect(cr, px, py, pw, ph, ph / 2);
    cairo_set_source_rgba(cr, C_LAYER1.r, C_LAYER1.g, C_LAYER1.b, C_LAYER1.a);
    cairo_fill(cr);

    double fill = o->value;
    if (fill < 0)
        fill = 0;
    if (fill > 1)
        fill = 1;
    if (fill > 0) {
        rounded_rect(cr, px, py, pw * fill, ph, ph / 2);
        if (o->muted)
            cairo_set_source_rgba(cr, C_SUBTEXT.r, C_SUBTEXT.g, C_SUBTEXT.b, 1);
        else
            cairo_set_source_rgba(cr, C_PRIMARY.r, C_PRIMARY.g, C_PRIMARY.b, 1);
        cairo_fill(cr);
    }

    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, opacity);

    cairo_destroy(cr);
    cairo_surface_flush(cs);

    wl_surface_attach(o->surface, o->buffer, 0, 0);
    wl_surface_damage_buffer(o->surface, 0, 0, o->buf_w, o->buf_h);
    wl_surface_commit(o->surface);

    cairo_surface_destroy(cs);
}

/* ---------------- layer surface ---------------- */

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
    struct osd *o = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w == 0)
        w = o->target && o->target->width > 0
                ? (uint32_t)o->target->width : 1366;
    if (h == 0)
        h = OSD_SURFACE_H;
    if ((int)w != o->buf_w || (int)h != o->buf_h)
        create_buffer(o, (int)w, (int)h);
    o->configured = 1;
    o->redraw = 1;
    draw(o);
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    (void)ls;
    struct osd *o = data;
    o->running = 0;
    g_running = 0;
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure,
    .closed = ls_closed,
};

static int create_surface(struct osd *o) {
    o->surface = wl_compositor_create_surface(o->compositor);
    if (!o->surface)
        return -1;

    struct wl_region *empty = wl_compositor_create_region(o->compositor);
    if (empty) {
        wl_surface_set_input_region(o->surface, empty);
        wl_region_destroy(empty);
    }

    o->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        o->layer_shell, o->surface, o->target->wl,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "dots:osd");
    if (!o->layer_surface)
        return -1;
    zwlr_layer_surface_v1_add_listener(o->layer_surface, &ls_listener, o);

    zwlr_layer_surface_v1_set_size(o->layer_surface, 0, OSD_SURFACE_H);
    uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                      ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (o->bar_bottom)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    else
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    zwlr_layer_surface_v1_set_anchor(o->layer_surface, anchor);

    uint32_t margin = (uint32_t)bar_height(o);
    if (o->bar_bottom)
        zwlr_layer_surface_v1_set_margin(o->layer_surface, 0, 0, margin, 0);
    else
        zwlr_layer_surface_v1_set_margin(o->layer_surface, margin, 0, 0, 0);

    zwlr_layer_surface_v1_set_exclusive_zone(o->layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        o->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(o->surface);
    return 0;
}

/* ---------------- commands ---------------- */

static int handle_command(struct osd *o, const char *msg) {
    char kind[16] = "", action[16] = "";
    double arg = 0;
    int n = sscanf(msg, "%15s %15s %lf", kind, action, &arg);
    if (n < 1)
        return -1;

    long long now = mono_ms();
    int was_visible = o->visible && opacity_at(o, now) > 0;

    if (strcmp(kind, "hide") == 0) {
        if (!o->visible)
            return 0;
        o->shown_ms = now - FADE_IN_MS;
        o->hide_at = now;
        o->redraw = 1;
        return 1;
    }
    if (n < 2)
        return -1;

    if (strcmp(kind, "volume") == 0)
        o->kind = OSD_VOLUME;
    else if (strcmp(kind, "brightness") == 0)
        o->kind = OSD_BRIGHTNESS;
    else
        return -1;

    if (strcmp(action, "up") == 0 || strcmp(action, "down") == 0 ||
        strcmp(action, "mute") == 0) {
        if (o->kind == OSD_VOLUME) {
            apply_volume(action);
            double v = o->value;
            int muted = o->muted;
            if (read_volume(&v, &muted) == 0) {
                o->value = v;
                o->muted = muted;
            } else if (strcmp(action, "up") == 0) {
                o->value += 0.02;
            } else if (strcmp(action, "down") == 0) {
                o->value -= 0.02;
            } else {
                o->muted = !o->muted;
            }
        } else if (o->kind == OSD_BRIGHTNESS) {
            if (strcmp(action, "mute") != 0) {
                apply_brightness(action);
                double v = o->value;
                if (read_brightness(&v) == 0)
                    o->value = v;
                else if (strcmp(action, "up") == 0)
                    o->value += 0.05;
                else
                    o->value -= 0.05;
            }
            o->muted = 0;
        }
    } else if (strcmp(action, "show") == 0 && n >= 3) {
        o->value = arg;
        o->muted = 0;
    } else {
        return -1;
    }

    if (o->value < 0)
        o->value = 0;
    if (o->value > 1)
        o->value = 1;

    o->visible = 1;
    if (!was_visible)
        o->shown_ms = now;
    o->hide_at = now + o->timeout_ms;
    o->redraw = 1;
    return 0;
}

static int build_message(char *buf, size_t size, int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "hide") == 0) {
        snprintf(buf, size, "hide");
        return 0;
    }
    if (argc < 3)
        return -1;

    const char *kind = NULL;
    const char *action = NULL;
    const char *value_arg = NULL;

    if (strcmp(argv[1], "show") == 0 && argc >= 4) {
        // Bentuk yang dipakai dokumen: dots-osd show volume 0.5
        kind = argv[2];
        action = "show";
        value_arg = argv[3];
    } else {
        kind = argv[1];
        action = argv[2];
        if (strcmp(action, "show") == 0 && argc >= 4)
            value_arg = argv[3];
    }

    if (strcmp(kind, "volume") != 0 && strcmp(kind, "brightness") != 0)
        return -1;
    if (strcmp(action, "up") == 0 || strcmp(action, "down") == 0) {
        snprintf(buf, size, "%s %s", kind, action);
        return 0;
    }
    if (strcmp(action, "mute") == 0) {
        // Brightness tidak punya mute pada QML.
        if (strcmp(kind, "volume") != 0)
            return -1;
        snprintf(buf, size, "%s mute", kind);
        return 0;
    }
    if (strcmp(action, "show") == 0 && value_arg) {
        char *end = NULL;
        double v = strtod(value_arg, &end);
        if (!end || *end || v < 0 || v > 1)
            return -1;
        snprintf(buf, size, "%s show %.6f", kind, v);
        return 0;
    }
    return -1;
}

static void cleanup(struct osd *o) {
    if (o->qs_suspended) {
        qs_osd_suspend(0);
        o->qs_suspended = 0;
    }
    if (o->buffer) {
        wl_buffer_destroy(o->buffer);
        o->buffer = NULL;
    }
    if (o->data) {
        munmap(o->data, (size_t)o->buf_w * (size_t)o->buf_h * 4);
        o->data = NULL;
    }
    if (o->layer_surface) {
        zwlr_layer_surface_v1_destroy(o->layer_surface);
        o->layer_surface = NULL;
    }
    if (o->surface) {
        wl_surface_destroy(o->surface);
        o->surface = NULL;
    }
    for (int i = 0; i < o->n_outputs; i++) {
        if (o->outputs[i].wl)
            wl_output_destroy(o->outputs[i].wl);
        o->outputs[i].wl = NULL;
    }
    if (o->sock_fd >= 0) {
        close(o->sock_fd);
        o->sock_fd = -1;
    }
    if (o->sock_owned && o->sock_path[0])
        unlink(o->sock_path);
    if (o->display)
        wl_display_disconnect(o->display);
    o->display = NULL;
}

static void usage(FILE *out) {
    fprintf(out,
        "usage: dots-osd <command>\n"
        "\n"
        "commands:\n"
        "  volume up|down|mute\n"
        "  brightness up|down\n"
        "  show volume|brightness <0.0-1.0>\n"
        "  hide\n"
        "\n"
        "OSD hidup hanya selama timeout; invocation berikutnya mengirim command\n"
        "ke instance yang sedang berjalan lewat socket runtime.\n");
}

int main(int argc, char **argv) {
    // Jangan sentuh LC_NUMERIC: parsing command memakai titik desimal.
    setlocale(LC_CTYPE, "");
    setlocale(LC_TIME, "");
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    dots_log_init_from_env();

    if (argc >= 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }

    char msg[256];
    if (build_message(msg, sizeof msg, argc, argv) < 0) {
        usage(stderr);
        return 2;
    }

    struct osd o;
    memset(&o, 0, sizeof o);
    o.sock_fd = -1;
    o.running = 1;
    o.kind = OSD_VOLUME;
    o.value = 0;
    o.sock_path[0] = 0;
    if (socket_path(o.sock_path, sizeof o.sock_path) < 0)
        return 1;

    // Existing instance? Hand the command over and exit.
    if (send_existing(o.sock_path, msg) == 0)
        return 0;

    // No active OSD and the command is only "hide".
    if (strcmp(msg, "hide") == 0)
        return 0;

    if (bind_socket(o.sock_path, &o.sock_fd) < 0) {
        int bind_errno = errno;
        // Another instance may have won the bind race after our first send.
        // Coba kirim lagi sebelum menganggap socket lama sebagai stale.
        if (bind_errno == EADDRINUSE &&
            send_existing(o.sock_path, msg) == 0)
            return 0;
        if (bind_errno != EADDRINUSE) {
            fprintf(stderr, "dots-osd: cannot bind %s: %s\n",
                    o.sock_path, strerror(bind_errno));
            return 1;
        }
        // Socket file lama yang tidak punya listener.
        unlink(o.sock_path);
        if (bind_socket(o.sock_path, &o.sock_fd) < 0) {
            fprintf(stderr, "dots-osd: cannot bind %s: %s\n",
                    o.sock_path, strerror(errno));
            return 1;
        }
    }
    o.sock_owned = 1;

    struct dots_config cfg;
    load_config(&o, &cfg);

    if (!getenv("DOTS_OSD_NO_QS_BRIDGE")) {
        qs_osd_suspend(1);
        o.qs_suspended = 1;
    }

    // Apply the command that launched this instance before creating the
    // surface, so the first frame already has the right value.
    if (handle_command(&o, msg) != 0) {
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 2;
    }
    if (!o.visible) {
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 0;
    }

    o.display = wl_display_connect(NULL);
    if (!o.display) {
        fprintf(stderr, "dots-osd: cannot connect to Wayland\n");
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 1;
    }

    o.registry = wl_display_get_registry(o.display);
    wl_registry_add_listener(o.registry, &registry_listener, &o);
    wl_display_roundtrip(o.display);
    wl_display_roundtrip(o.display);

    if (!o.compositor || !o.shm || !o.layer_shell) {
        fprintf(stderr, "dots-osd: missing wayland globals\n");
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 1;
    }

    char wanted[64] = "";
    focused_output_name(wanted, sizeof wanted);
    o.target = select_output(&o, wanted);
    if (!o.target) {
        fprintf(stderr, "dots-osd: no output\n");
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 1;
    }

    if (create_surface(&o) < 0) {
        fprintf(stderr, "dots-osd: cannot create layer surface\n");
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 1;
    }

    // Wait for the first configure. The callback creates the buffer and
    // draws the card.
    for (int i = 0; i < 50 && g_running && !o.configured; i++)
        wl_display_roundtrip(o.display);

    if (!o.configured) {
        fprintf(stderr, "dots-osd: layer surface was not configured\n");
        cleanup(&o);
        dots_config_destroy(&cfg);
        return 1;
    }

    while (g_running) {
        wl_display_dispatch_pending(o.display);
        wl_display_flush(o.display);

        long long now = mono_ms();
        if (o.redraw) {
            o.redraw = 0;
            draw(&o);
        }

        if (o.visible && now >= o.hide_at + FADE_OUT_MS)
            break;

        int timeout = 200;
        if (!o.visible) {
            timeout = 0;
        } else if (now < o.shown_ms + FADE_IN_MS) {
            timeout = 16;
        } else if (now < o.hide_at) {
            long long wait = o.hide_at - now;
            timeout = wait > 200 ? 200 : (int)wait;
        } else if (now < o.hide_at + FADE_OUT_MS) {
            timeout = 16;
        } else {
            timeout = 0;
        }

        struct pollfd fds[2] = {
            { .fd = wl_display_get_fd(o.display), .events = POLLIN },
            { .fd = o.sock_fd, .events = POLLIN },
        };
        int pr = poll(fds, 2, timeout);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (pr > 0 && (fds[0].revents & POLLIN)) {
            if (wl_display_dispatch(o.display) < 0)
                break;
        }
        if (pr > 0 && (fds[1].revents & POLLIN)) {
            char buf[256];
            ssize_t n;
            while ((n = recv(o.sock_fd, buf, sizeof buf - 1, MSG_DONTWAIT)) > 0) {
                buf[n] = 0;
                (void)handle_command(&o, buf);
            }
        }
    }

    cleanup(&o);
    dots_config_destroy(&cfg);
    return 0;
}
