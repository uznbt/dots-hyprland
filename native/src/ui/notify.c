// dots-notify - server notifikasi D-Bus + popup Wayland native.
//
// Tahap transisi: proses ini menggabungkan server org.freedesktop.Notifications
// dan layer surface popup. Server sengaja tidak dinyalakan oleh dots-session
// sebelum popup stabil; Quickshell tetap menjadi notification server fallback.
// Saat backend native diaktifkan, dots-notify dapat menggantikan Quickshell
// tanpa perlu mengubah nama D-Bus.

#define _GNU_SOURCE
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <linux/input-event-codes.h>

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

#include "common/config.h"
#include "common/log.h"
#include "common/paths.h"

#include "wlr-layer-shell-client.h"

#define NOTIFY_WIDTH 410
#define NOTIFY_MARGIN 4
#define NOTIFY_CARD_W (NOTIFY_WIDTH - NOTIFY_MARGIN * 2)
#define NOTIFY_MAX 64
#define NOTIFY_MAX_ACTIONS 8
#define NOTIFY_MAX_VISIBLE 5
#define NOTIFY_DEFAULT_TIMEOUT 7000
#define NOTIFY_TICK_MS 100
#define NOTIFY_MAX_OUTPUTS 16
#define NOTIFY_MAX_TEXT 8192
#define NOTIFY_MAX_SUMMARY 1024
#define NOTIFY_MAX_APP 256
#define NOTIFY_MAX_ICON 1024
#define NOTIFY_MAX_ACTION 256

#define VAR_MAIN "wght=450,wdth=100,opsz=100"
#define VAR_SUMMARY "wght=500,wdth=100,opsz=100"

struct rgba {
    double r, g, b, a;
};

// Warna fallback mengikuti generated/colors.json milik Quickshell. Nilainya
// dimuat ulang saat startup supaya popup tidak tampil dengan palet prototipe.
static struct rgba C_CARD = { 27 / 255.0, 33 / 255.0, 30 / 255.0, 0.97 };
static struct rgba C_TEXT = { 222 / 255.0, 228 / 255.0, 224 / 255.0, 1.0 };
static struct rgba C_SUBTEXT = { 191 / 255.0, 201 / 255.0, 196 / 255.0, 1.0 };
static struct rgba C_OUTLINE = { 137 / 255.0, 147 / 255.0, 142 / 255.0, 1.0 };
static struct rgba C_CRITICAL = { 52 / 255.0, 76 / 255.0, 68 / 255.0, 1.0 };
static struct rgba C_ON_CRITICAL = { 205 / 255.0, 233 / 255.0, 222 / 255.0, 1.0 };
static struct rgba C_ACTION = { 48 / 255.0, 54 / 255.0, 51 / 255.0, 1.0 };
static struct rgba C_ACTION_HOVER = { 64 / 255.0, 70 / 255.0, 67 / 255.0, 1.0 };
static struct rgba C_SHADOW = { 0, 0, 0, 0.28 };

struct notify_action {
    char *id;
    char *label;
};

struct notification {
    uint32_t id;
    char *app_name;
    char *app_icon;
    char *summary;
    char *body;
    char *image_path;
    cairo_surface_t *icon_surface;
    guint8 urgency;
    gboolean transient;
    struct notify_action actions[NOTIFY_MAX_ACTIONS];
    int n_actions;
    int expire_timeout;
    long long expires_at;
    long long paused_remaining;
    int paused;
    int expanded;
    int visible;
    int card_y;
    int card_h;
    int toggle_x, toggle_y, toggle_w, toggle_h;
    int close_x, close_y, close_w, close_h;
    int action_x[NOTIFY_MAX_ACTIONS];
    int action_y[NOTIFY_MAX_ACTIONS];
    int action_w[NOTIFY_MAX_ACTIONS];
    int action_h[NOTIFY_MAX_ACTIONS];
};

struct notify_output {
    struct wl_output *wl;
    uint32_t global_name;
    char name[64];
    int width;
    int height;
};

struct notify_state {
    int serve;
    int no_ui;

    struct dots_config cfg;
    int timeout_ms;

    // Wayland
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_output *target;
    struct notify_output outputs[NOTIFY_MAX_OUTPUTS];
    int n_outputs;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer *buffer;
    void *data;
    int buf_w, buf_h;
    int configured;
    int requested_h;
    int surface_h;
    int pointer_inside;
    double pointer_x, pointer_y;
    uint32_t hovered_id;
    int redraw;
    guint wayland_source;

    // Server dan state notifikasi
    struct notification items[NOTIFY_MAX];
    int count;
    uint32_t next_id;
    guint tick_source;
    guint dbus_owner;
    GDBusConnection *bus;
    GDBusNodeInfo *node;
    guint registration;
    int dbus_ready;
    int quitting;
    GMainLoop *loop;
};

static const struct wl_seat_listener seat_listener;

static const char introspection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='Notify'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='as' direction='in'/>"
    "      <arg type='a{sv}' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='u' direction='out'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg type='as' direction='out'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg type='s' direction='out'/>"
    "      <arg type='s' direction='out'/>"
    "      <arg type='s' direction='out'/>"
    "      <arg type='s' direction='out'/>"
    "    </method>"
    "    <signal name='NotificationClosed'>"
    "      <arg type='u'/>"
    "      <arg type='u'/>"
    "    </signal>"
    "    <signal name='ActionInvoked'>"
    "      <arg type='u'/>"
    "      <arg type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static struct rgba mix_color(struct rgba a, struct rgba b, double t) {
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    struct rgba out = {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
    return out;
}

/* ---------------- teks ---------------- */

static char *clean_line(const char *text, size_t max) {
    if (!text)
        return g_strdup("");
    GString *out = g_string_new(NULL);
    int last_space = 0;
    for (const char *p = text; *p && out->len < max; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c == ' ' && last_space)
            continue;
        if (c < 0x20)
            continue;
        g_string_append_c(out, (char)c);
        last_space = (c == ' ');
    }
    while (out->len && out->str[out->len - 1] == ' ')
        g_string_truncate(out, out->len - 1);
    return g_string_free(out, FALSE);
}

static char *clean_body(const char *text, size_t max) {
    if (!text)
        return g_strdup("");
    GString *out = g_string_new(NULL);
    for (const char *p = text; *p && out->len < max; p++) {
        if (*p == '<') {
            const char *end = strchr(p, '>');
            if (!end)
                break;
            if (g_ascii_strncasecmp(p, "<br", 3) == 0 ||
                g_ascii_strncasecmp(p, "<br/", 4) == 0)
                g_string_append_c(out, '\n');
            p = end;
            continue;
        }
        if (*p == '&') {
            if (g_str_has_prefix(p, "&amp;")) {
                g_string_append_c(out, '&');
                p += 5;
                continue;
            }
            if (g_str_has_prefix(p, "&lt;")) {
                g_string_append_c(out, '<');
                p += 4;
                continue;
            }
            if (g_str_has_prefix(p, "&gt;")) {
                g_string_append_c(out, '>');
                p += 4;
                continue;
            }
            if (g_str_has_prefix(p, "&quot;")) {
                g_string_append_c(out, '"');
                p += 6;
                continue;
            }
            if (g_str_has_prefix(p, "&#39;")) {
                g_string_append_c(out, '\'');
                p += 5;
                continue;
            }
        }
        if (*p == '\r')
            continue;
        g_string_append_c(out, *p);
    }
    // Jangan biarkan body berisi puluhan newline kosong.
    while (out->len > 2 &&
           out->str[out->len - 1] == '\n' && out->str[out->len - 2] == '\n')
        g_string_truncate(out, out->len - 1);
    while (out->len && out->str[out->len - 1] == '\n')
        g_string_truncate(out, out->len - 1);
    return g_string_free(out, FALSE);
}

static PangoLayout *make_layout(cairo_t *cr, const char *text, double px,
                                int width, int max_lines, int summary) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, "Google Sans Flex");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_font_description_set_variations(d, summary ? VAR_SUMMARY : VAR_MAIN);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, text ? text : "", -1);
    pango_layout_set_single_paragraph_mode(l, summary ? TRUE : FALSE);
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    if (width > 0)
        pango_layout_set_width(l, width * PANGO_SCALE);
    if (max_lines > 0) {
        pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
        pango_layout_set_height(l, -max_lines);
    }
    return l;
}

static PangoLayout *make_icon_layout(cairo_t *cr, const char *name, double px) {
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
    pango_layout_set_text(l, name ? name : "", -1);
    return l;
}

static void draw_text(cairo_t *cr, const char *text, double px, double x,
                      double y, double width, int max_lines, int summary,
                      struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px, (int)width, max_lines, summary);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_icon(cairo_t *cr, const char *name, double px, double cx,
                      double cy, struct rgba col) {
    PangoLayout *l = make_icon_layout(cr, name, px);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
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

/* ---------------- tema ---------------- */

static int parse_hex_color(const char *text, struct rgba *out) {
    if (!text || text[0] != '#')
        return 0;
    size_t n = strlen(text);
    if (n != 7 && n != 9)
        return 0;
    char buf[3] = { 0 };
    unsigned vals[4] = { 0, 0, 0, 255 };
    int count = (n == 9) ? 4 : 3;
    for (int i = 0; i < count; i++) {
        buf[0] = text[1 + i * 2];
        buf[1] = text[2 + i * 2];
        char *end = NULL;
        unsigned long v = strtoul(buf, &end, 16);
        if (!end || *end || v > 255)
            return 0;
        vals[i] = (unsigned)v;
    }
    out->r = vals[0] / 255.0;
    out->g = vals[1] / 255.0;
    out->b = vals[2] / 255.0;
    out->a = vals[3] / 255.0;
    return 1;
}

static int theme_color(struct json_object *root, const char *key,
                       struct rgba *out, double alpha) {
    json_object *v = NULL;
    if (!json_object_object_get_ex(root, key, &v) ||
        !json_object_is_type(v, json_type_string))
        return 0;
    if (!parse_hex_color(json_object_get_string(v), out))
        return 0;
    out->a = alpha;
    return 1;
}

static void load_theme(void) {
    const char *state = g_getenv("XDG_STATE_HOME");
    char path[768];
    if (state && state[0])
        snprintf(path, sizeof path, "%s/quickshell/user/generated/colors.json",
                 state);
    else
        snprintf(path, sizeof path,
                 "%s/.local/state/quickshell/user/generated/colors.json",
                 g_get_home_dir());

    struct json_object *root = json_object_from_file(path);
    if (!root)
        return;

    struct rgba card = C_CARD, text = C_TEXT, subtext = C_SUBTEXT;
    struct rgba outline = C_OUTLINE, critical = C_CRITICAL;
    struct rgba on_critical = C_ON_CRITICAL, action = C_ACTION;
    struct rgba action_hover = C_ACTION_HOVER;

    theme_color(root, "surface_container", &card, 0.97);
    theme_color(root, "on_surface", &text, 1.0);
    theme_color(root, "on_surface_variant", &subtext, 1.0);
    theme_color(root, "outline", &outline, 1.0);
    theme_color(root, "secondary_container", &critical, 1.0);
    theme_color(root, "on_secondary_container", &on_critical, 1.0);
    theme_color(root, "surface_container_highest", &action, 0.96);
    theme_color(root, "surface_container_high", &action_hover, 0.96);
    json_object_put(root);

    C_CARD = card;
    C_TEXT = text;
    C_SUBTEXT = subtext;
    C_OUTLINE = outline;
    C_CRITICAL = critical;
    C_ON_CRITICAL = on_critical;
    C_ACTION = action;
    C_ACTION_HOVER = action_hover;
}

/* ---------------- util wayland ---------------- */

static int create_shm_file(size_t size) {
    int fd = memfd_create("dots-notify", MFD_CLOEXEC);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }
    char name[64];
    snprintf(name, sizeof name, "/dots-notify-%d", getpid());
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

static void output_geometry(void *data, struct wl_output *wl, int32_t x,
                            int32_t y, int32_t pw, int32_t ph, int32_t subpixel,
                            const char *make, const char *model,
                            int32_t transform) {
    (void)data; (void)wl; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *wl, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    (void)wl; (void)flags; (void)refresh;
    struct notify_output *out = data;
    out->width = width;
    out->height = height;
}

static void output_name(void *data, struct wl_output *wl, const char *name) {
    (void)wl;
    struct notify_output *out = data;
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
    struct notify_state *n = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        n->compositor = wl_registry_bind(registry, name,
                                         &wl_compositor_interface,
                                         version < 4 ? version : 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        n->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        n->layer_shell = wl_registry_bind(registry, name,
                                          &zwlr_layer_shell_v1_interface,
                                          version < 4 ? version : 4);
    } else if (strcmp(iface, wl_output_interface.name) == 0 &&
               n->n_outputs < NOTIFY_MAX_OUTPUTS) {
        struct notify_output *out = &n->outputs[n->n_outputs++];
        memset(out, 0, sizeof *out);
        out->global_name = name;
        out->wl = wl_registry_bind(registry, name, &wl_output_interface,
                                   version < 4 ? version : 4);
        if (out->wl)
            wl_output_add_listener(out->wl, &output_listener, out);
    } else if (strcmp(iface, wl_seat_interface.name) == 0 && !n->seat) {
        n->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                   version < 7 ? version : 7);
        if (n->seat)
            wl_seat_add_listener(n->seat, &seat_listener, n);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
    (void)registry;
    struct notify_state *n = data;
    for (int i = 0; i < n->n_outputs; i++) {
        if (n->outputs[i].global_name != name)
            continue;
        if (n->target == n->outputs[i].wl)
            n->target = NULL;
        if (n->outputs[i].wl)
            wl_output_destroy(n->outputs[i].wl);
        memmove(&n->outputs[i], &n->outputs[i + 1],
                (size_t)(n->n_outputs - i - 1) * sizeof n->outputs[0]);
        n->n_outputs--;
        return;
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ---------------- pemilihan output ---------------- */

static char *run_capture(const char *const argv[]) {
    GError *error = NULL;
    char *out = NULL;
    int status = 0;
    if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, &out, NULL, &status, &error)) {
        if (error)
            g_error_free(error);
        return NULL;
    }
    if (!g_spawn_check_wait_status(status, &error)) {
        if (error)
            g_error_free(error);
        g_free(out);
        return NULL;
    }
    return out;
}

static void config_output_name(struct notify_state *n, char *out, size_t size) {
    out[0] = 0;
    const char *fixed = g_getenv("DOTS_OUTPUT");
    if (fixed && fixed[0]) {
        g_strlcpy(out, fixed, size);
        return;
    }

    if (dots_config_bool(&n->cfg, "notifications.monitor.enable", 0)) {
        const char *name = dots_config_string(&n->cfg,
                                              "notifications.monitor.name", "");
        if (name && name[0]) {
            g_strlcpy(out, name, size);
            return;
        }
    }
    if (dots_config_bool(&n->cfg, "notifications.forceMonitor.enable", 0)) {
        const char *name = dots_config_string(&n->cfg,
                                              "notifications.forceMonitor.name", "");
        if (name && name[0]) {
            g_strlcpy(out, name, size);
            return;
        }
    }

    char *json = run_capture((const char *const[]){ "hyprctl", "monitors", "-j",
                                                    NULL });
    if (!json)
        return;
    struct json_object *root = json_tokener_parse(json);
    g_free(json);
    if (!root || !json_object_is_type(root, json_type_array)) {
        if (root)
            json_object_put(root);
        return;
    }
    size_t len = json_object_array_length(root);
    for (size_t i = 0; i < len; i++) {
        struct json_object *mon = json_object_array_get_idx(root, i);
        struct json_object *focused = NULL, *name = NULL;
        if (!json_object_is_type(mon, json_type_object))
            continue;
        json_object_object_get_ex(mon, "focused", &focused);
        json_object_object_get_ex(mon, "name", &name);
        if (focused && json_object_get_boolean(focused) && name &&
            json_object_is_type(name, json_type_string)) {
            g_strlcpy(out, json_object_get_string(name), size);
            break;
        }
    }
    json_object_put(root);
}

static struct notify_output *select_output(struct notify_state *n,
                                           const char *wanted) {
    if (wanted && wanted[0]) {
        for (int i = 0; i < n->n_outputs; i++) {
            if (n->outputs[i].wl &&
                strcmp(n->outputs[i].name, wanted) == 0)
                return &n->outputs[i];
        }
    }
    for (int i = 0; i < n->n_outputs; i++) {
        if (n->outputs[i].wl && n->outputs[i].name[0])
            return &n->outputs[i];
    }
    for (int i = 0; i < n->n_outputs; i++) {
        if (n->outputs[i].wl)
            return &n->outputs[i];
    }
    return NULL;
}

/* ---------------- pointer ---------------- */

static struct notification *item_by_id(struct notify_state *n, uint32_t id) {
    for (int i = 0; i < n->count; i++) {
        if (n->items[i].id == id)
            return &n->items[i];
    }
    return NULL;
}

static struct notification *hit_card(struct notify_state *n, double x,
                                     double y) {
    if (x < NOTIFY_MARGIN || x > NOTIFY_WIDTH - NOTIFY_MARGIN)
        return NULL;
    for (int i = 0; i < n->count; i++) {
        struct notification *item = &n->items[i];
        if (!item->visible)
            continue;
        if (y >= item->card_y && y < item->card_y + item->card_h)
            return item;
    }
    return NULL;
}

static void pause_item(struct notify_state *n, struct notification *item,
                       int pause) {
    (void)n;
    if (item->expire_timeout == 0)
        return;
    long long now = mono_ms();
    if (pause) {
        if (item->paused)
            return;
        item->paused_remaining = item->expires_at - now;
        if (item->paused_remaining < 0)
            item->paused_remaining = 0;
        item->paused = 1;
        DOTS_DEBUG("notify: pause id=%u remaining=%lld", item->id,
                   item->paused_remaining);
    } else if (item->paused) {
        item->paused = 0;
        item->expires_at = now + item->paused_remaining;
        DOTS_DEBUG("notify: resume id=%u remaining=%lld", item->id,
                   item->paused_remaining);
    }
}

static void set_hovered(struct notify_state *n, struct notification *item) {
    uint32_t old_id = n->hovered_id;
    if (item && item->id == old_id)
        return;
    if (old_id) {
        struct notification *old = item_by_id(n, old_id);
        if (old)
            pause_item(n, old, 0);
    }
    n->hovered_id = item ? item->id : 0;
    if (item)
        pause_item(n, item, 1);
    if (n->surface)
        n->redraw = 1;
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)serial; (void)surface;
    struct notify_state *n = data;
    n->pointer_inside = 1;
    n->pointer_x = wl_fixed_to_double(sx);
    n->pointer_y = wl_fixed_to_double(sy);
    DOTS_DEBUG("notify: pointer enter %.0f,%.0f", n->pointer_x, n->pointer_y);
    set_hovered(n, hit_card(n, n->pointer_x, n->pointer_y));
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {
    (void)pointer; (void)serial; (void)surface;
    struct notify_state *n = data;
    n->pointer_inside = 0;
    set_hovered(n, NULL);
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    struct notify_state *n = data;
    n->pointer_x = wl_fixed_to_double(sx);
    n->pointer_y = wl_fixed_to_double(sy);
    struct notification *hit = hit_card(n, n->pointer_x, n->pointer_y);
    set_hovered(n, hit);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t source) {
    (void)data; (void)pointer; (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete) {
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t value120) {
    (void)data; (void)pointer; (void)axis; (void)value120;
}

static void pointer_axis_relative_direction(void *data,
                                            struct wl_pointer *pointer,
                                            uint32_t axis,
                                            uint32_t direction) {
    (void)data; (void)pointer; (void)axis; (void)direction;
}

static void close_notification(struct notify_state *n, uint32_t id,
                               uint32_t reason);
static void reflow(struct notify_state *n);

static void toggle_expanded(struct notify_state *n, struct notification *item) {
    (void)n;
    item->expanded = !item->expanded;
}

static void invoke_action(struct notify_state *n, struct notification *item,
                          int action_index) {
    if (!item || action_index < 0 || action_index >= item->n_actions)
        return;
    char *id = g_strdup(item->actions[action_index].id);
    if (n->dbus_ready && n->bus) {
        g_dbus_connection_emit_signal(
            n->bus, NULL, "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "ActionInvoked",
            g_variant_new("(us)", item->id, id), NULL);
    }
    uint32_t notif_id = item->id;
    g_free(id);
    close_notification(n, notif_id, 2);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    struct notify_state *n = data;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    struct notification *item = hit_card(n, n->pointer_x, n->pointer_y);
    if (!item)
        return;

    if (button == BTN_RIGHT) {
        toggle_expanded(n, item);
        reflow(n);
        return;
    }
    if (button == BTN_MIDDLE) {
        close_notification(n, item->id, 2);
        return;
    }
    if (button != BTN_LEFT)
        return;

    int x = (int)n->pointer_x;
    int y = (int)n->pointer_y;
    if (x >= item->toggle_x && x < item->toggle_x + item->toggle_w &&
        y >= item->toggle_y && y < item->toggle_y + item->toggle_h) {
        toggle_expanded(n, item);
        reflow(n);
        return;
    }
    if (!item->expanded)
        return;
    if (x >= item->close_x && x < item->close_x + item->close_w &&
        y >= item->close_y && y < item->close_y + item->close_h) {
        close_notification(n, item->id, 2);
        return;
    }
    for (int i = 0; i < item->n_actions; i++) {
        if (x >= item->action_x[i] &&
            x < item->action_x[i] + item->action_w[i] &&
            y >= item->action_y[i] &&
            y < item->action_y[i] + item->action_h[i]) {
            invoke_action(n, item, i);
            return;
        }
    }
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_relative_direction,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t caps) {
    struct notify_state *n = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !n->pointer) {
        n->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(n->pointer, &pointer_listener, n);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && n->pointer) {
        wl_pointer_destroy(n->pointer);
        n->pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* ---------------- render popup ---------------- */

static cairo_surface_t *load_png_icon(const char *icon) {
    if (!icon || !icon[0])
        return NULL;
    const char *path = icon;
    if (g_str_has_prefix(path, "file://"))
        path += 7;
    if (g_path_is_absolute(path) && g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        cairo_surface_t *s = cairo_image_surface_create_from_png(path);
        if (s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS)
            return s;
        if (s)
            cairo_surface_destroy(s);
        return NULL;
    }

    if (strchr(path, '/'))
        return NULL;
    static const char *dirs[] = {
        "/usr/share/icons/hicolor/48x48/apps",
        "/usr/share/icons/hicolor/64x64/apps",
        "/usr/share/icons/hicolor/128x128/apps",
        "/usr/share/icons/hicolor/256x256/apps",
        "/usr/share/pixmaps",
        NULL,
    };
    for (int i = 0; dirs[i]; i++) {
        char candidate[1024];
        snprintf(candidate, sizeof candidate, "%s/%s.png", dirs[i], path);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR)) {
            cairo_surface_t *s = cairo_image_surface_create_from_png(candidate);
            if (s && cairo_surface_status(s) == CAIRO_STATUS_SUCCESS)
                return s;
            if (s)
                cairo_surface_destroy(s);
        }
    }
    return NULL;
}

static void draw_icon_badge(cairo_t *cr, struct notification *item, double x,
                            double y, double size, struct rgba fg) {
    struct rgba bg = item->urgency >= 2 ? C_CRITICAL : C_ACTION;
    rounded_rect(cr, x, y, size, size, 10);
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_fill(cr);

    cairo_surface_t *icon = item->icon_surface;
    if (icon) {
        double iw = cairo_image_surface_get_width(icon);
        double ih = cairo_image_surface_get_height(icon);
        if (iw > 0 && ih > 0) {
            cairo_save(cr);
            rounded_rect(cr, x + 3, y + 3, size - 6, size - 6, 8);
            cairo_clip(cr);
            double scale = fmin((size - 6) / iw, (size - 6) / ih);
            cairo_translate(cr, x + 3 + ((size - 6) - iw * scale) / 2,
                            y + 3 + ((size - 6) - ih * scale) / 2);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, icon, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
        return;
    }

    const char *glyph = item->urgency >= 2 ? "priority_high" : "notifications";
    draw_icon(cr, glyph, size * 0.55, x + size / 2, y + size / 2, fg);
}

static int card_height(struct notification *item, cairo_t *cr, int width) {
    if (!item->expanded)
        return 80;

    int content_w = width - 12 - 40 - 10 - 12;
    if (content_w < 40)
        content_w = 40;
    int body_h = 0;
    if (item->body && item->body[0]) {
        PangoLayout *l = make_layout(cr, item->body, 11.5, content_w, 8, 0);
        int w = 0;
        pango_layout_get_pixel_size(l, &w, &body_h);
        g_object_unref(l);
    }
    int action_h = 8 + 34;
    int h = 10 + 18 + 4 + body_h + 14 + action_h + 10;
    if (h < 80)
        h = 80;
    if (h > 420)
        h = 420;
    return h;
}

static void draw_action_button(cairo_t *cr, struct notification *item,
                               double x, double y, double w, double h,
                               const char *label, int hovered) {
    struct rgba bg = item->urgency >= 2 ? C_CRITICAL : C_ACTION;
    if (hovered)
        bg = item->urgency >= 2 ? mix_color(C_CRITICAL, C_ON_CRITICAL, 0.10)
                                : C_ACTION_HOVER;
    rounded_rect(cr, x, y, w, h, 10);
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_fill(cr);
    struct rgba fg = item->urgency >= 2 ? C_ON_CRITICAL : C_TEXT;
    PangoLayout *l = make_layout(cr, label, 11.5, (int)w, 1, 1);
    int tw = 0, th = 0;
    pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
    pango_layout_get_pixel_size(l, &tw, &th);
    (void)tw;
    cairo_set_source_rgba(cr, fg.r, fg.g, fg.b, fg.a);
    cairo_move_to(cr, x, y + (h - th) / 2.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_icon_button(cairo_t *cr, struct notification *item,
                             double x, double y, double w, double h,
                             const char *glyph, int hovered) {
    struct rgba bg = item->urgency >= 2 ? C_CRITICAL : C_ACTION;
    if (hovered)
        bg = item->urgency >= 2 ? mix_color(C_CRITICAL, C_ON_CRITICAL, 0.10)
                                : C_ACTION_HOVER;
    rounded_rect(cr, x, y, w, h, 10);
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_fill(cr);
    struct rgba fg = item->urgency >= 2 ? C_ON_CRITICAL : C_TEXT;
    draw_icon(cr, glyph, 17, x + w / 2.0, y + h / 2.0, fg);
}

static void draw_card(struct notify_state *n, cairo_t *cr,
                      struct notification *item) {
    (void)n;
    const double x = NOTIFY_MARGIN;
    const double y = item->card_y;
    const double w = NOTIFY_CARD_W;
    const double h = item->card_h;
    const double radius = 17;
    int critical = item->urgency >= 2;

    // Shadow.
    rounded_rect(cr, x + 1, y + 2, w, h, radius);
    cairo_set_source_rgba(cr, C_SHADOW.r, C_SHADOW.g, C_SHADOW.b, C_SHADOW.a);
    cairo_fill(cr);

    // Background.
    struct rgba bg = critical ? C_CRITICAL : C_CARD;
    rounded_rect(cr, x, y, w, h, radius);
    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
    cairo_fill(cr);

    rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, radius - 0.5);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, C_OUTLINE.r, C_OUTLINE.g, C_OUTLINE.b, 0.22);
    cairo_stroke(cr);

    struct rgba fg = critical ? C_ON_CRITICAL : C_TEXT;
    struct rgba sub = critical ? C_ON_CRITICAL : C_SUBTEXT;
    double content_x = x + 12 + 40 + 10;
    double content_w = w - (content_x - x) - 12;

    draw_icon_badge(cr, item, x + 12, y + 11, 40, fg);

    // Ringkasan dan tombol expand.
    item->toggle_w = 26;
    item->toggle_h = 26;
    item->toggle_x = (int)(x + w - 12 - item->toggle_w);
    item->toggle_y = (int)(y + 7);
    draw_text(cr, item->summary, 13, content_x, y + 9,
              content_w - item->toggle_w - 4, 1, 1, fg);
    draw_icon(cr, item->expanded ? "expand_less" : "expand_more", 17,
              item->toggle_x + item->toggle_w / 2.0,
              item->toggle_y + item->toggle_h / 2.0, sub);

    // Body.
    int body_lines = item->expanded ? 8 : 1;
    double body_y = y + 31;
    draw_text(cr, item->body, 11.5, content_x, body_y, content_w,
              body_lines, 0, sub);

    if (!item->expanded) {
        draw_text(cr, item->app_name, 9.5, content_x, y + 61,
                  content_w, 1, 0, sub);
    }

    // Action row.
    int row_y = (int)(y + h - 10 - 34);
    item->close_w = 34;
    item->close_h = 34;
    item->close_x = (int)(x + 12);
    item->close_y = row_y;
    item->action_h[0] = 34;
    item->action_y[0] = row_y;

    if (item->expanded) {
        // Nama aplikasi kecil di atas tombol.
        draw_text(cr, item->app_name, 9.5, x + 12,
                  row_y - 16, w - 24, 1, 0, sub);

        int mouse_x = (int)n->pointer_x;
        int mouse_y = (int)n->pointer_y;
        int hover_close = n->pointer_inside &&
            mouse_x >= item->close_x &&
            mouse_x < item->close_x + item->close_w &&
            mouse_y >= item->close_y &&
            mouse_y < item->close_y + item->close_h;
        draw_icon_button(cr, item, item->close_x, item->close_y,
                         item->close_w, item->close_h, "close",
                         hover_close);

        double ax = item->close_x + item->close_w + 6;
        double max_x = x + w - 12;
        for (int i = 0; i < item->n_actions; i++) {
            double px = 11.5;
            PangoLayout *l = make_layout(cr, item->actions[i].label, px, 0, 1, 1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(l, &tw, &th);
            g_object_unref(l);
            double bw = tw + 24;
            if (bw > 170)
                bw = 170;
            if (ax + bw > max_x)
                break;
            int hover = n->pointer_inside && mouse_x >= ax &&
                mouse_x < ax + bw && mouse_y >= row_y &&
                mouse_y < row_y + 34;
            draw_action_button(cr, item, ax, row_y, bw, 34,
                               item->actions[i].label, hover);
            item->action_x[i] = (int)ax;
            item->action_w[i] = (int)bw;
            ax += bw + 6;
        }
    }
}

static void draw(struct notify_state *n) {
    if (!n->configured || !n->data || !n->buffer || n->no_ui)
        return;

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        n->data, CAIRO_FORMAT_ARGB32, n->buf_w, n->buf_h, n->buf_w * 4);
    cairo_t *cr = cairo_create(cs);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    for (int i = 0; i < n->count; i++) {
        if (n->items[i].visible)
            draw_card(n, cr, &n->items[i]);
    }

    cairo_destroy(cr);
    cairo_surface_flush(cs);

    wl_surface_attach(n->surface, n->buffer, 0, 0);
    wl_surface_damage_buffer(n->surface, 0, 0, n->buf_w, n->buf_h);
    wl_surface_commit(n->surface);
    cairo_surface_destroy(cs);
    n->redraw = 0;
}

static void create_buffer(struct notify_state *n, int w, int h) {
    if (n->buffer) {
        wl_buffer_destroy(n->buffer);
        n->buffer = NULL;
    }
    if (n->data) {
        munmap(n->data, (size_t)n->buf_w * (size_t)n->buf_h * 4);
        n->data = NULL;
    }
    if (w <= 0 || h <= 0)
        return;

    size_t size = (size_t)w * (size_t)h * 4;
    int fd = create_shm_file(size);
    if (fd < 0)
        return;
    n->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (n->data == MAP_FAILED) {
        n->data = NULL;
        close(fd);
        return;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(n->shm, fd, (int)size);
    n->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4,
                                          WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    n->buf_w = w;
    n->buf_h = h;
}

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                            uint32_t serial, uint32_t w, uint32_t h) {
    struct notify_state *n = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w == 0)
        w = NOTIFY_WIDTH;
    if (h == 0)
        h = (uint32_t)(n->surface_h > 0 ? n->surface_h : 1);
    n->surface_h = (int)h;
    n->requested_h = (int)h;
    create_buffer(n, (int)w, (int)h);
    n->configured = 1;
    draw(n);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    (void)ls;
    struct notify_state *n = data;
    n->configured = 0;
    if (n->buffer) {
        wl_buffer_destroy(n->buffer);
        n->buffer = NULL;
    }
    if (n->data) {
        munmap(n->data, (size_t)n->buf_w * (size_t)n->buf_h * 4);
        n->data = NULL;
    }
    if (n->layer_surface) {
        zwlr_layer_surface_v1_destroy(n->layer_surface);
        n->layer_surface = NULL;
    }
    if (n->surface) {
        wl_surface_destroy(n->surface);
        n->surface = NULL;
    }
    n->requested_h = 0;
    n->surface_h = 0;
    n->hovered_id = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

static void apply_input_region(struct notify_state *n) {
    if (!n->surface)
        return;
    struct wl_region *region = wl_compositor_create_region(n->compositor);
    if (!region)
        return;
    for (int i = 0; i < n->count; i++) {
        struct notification *item = &n->items[i];
        if (!item->visible)
            continue;
        wl_region_add(region, NOTIFY_MARGIN, item->card_y,
                      NOTIFY_CARD_W, item->card_h);
    }
    wl_surface_set_input_region(n->surface, region);
    wl_region_destroy(region);
}

static int ensure_surface(struct notify_state *n, int height) {
    if (n->no_ui || !n->display)
        return 0;
    if (!n->target)
        return -1;
    if (n->surface)
        return 0;

    n->surface = wl_compositor_create_surface(n->compositor);
    if (!n->surface)
        return -1;
    n->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        n->layer_shell, n->surface, n->target,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "dots:notification");
    if (!n->layer_surface) {
        wl_surface_destroy(n->surface);
        n->surface = NULL;
        return -1;
    }
    zwlr_layer_surface_v1_add_listener(n->layer_surface, &layer_listener, n);
    zwlr_layer_surface_v1_set_size(n->layer_surface, NOTIFY_WIDTH,
                                   height > 0 ? (uint32_t)height : 1);
    zwlr_layer_surface_v1_set_anchor(
        n->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_margin(n->layer_surface, NOTIFY_MARGIN,
                                     NOTIFY_MARGIN, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(n->layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        n->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    n->surface_h = height;
    n->requested_h = height;
    n->configured = 0;
    apply_input_region(n);
    wl_surface_commit(n->surface);
    wl_display_flush(n->display);
    return 0;
}

static void destroy_surface(struct notify_state *n) {
    if (n->buffer) {
        wl_buffer_destroy(n->buffer);
        n->buffer = NULL;
    }
    if (n->data) {
        munmap(n->data, (size_t)n->buf_w * (size_t)n->buf_h * 4);
        n->data = NULL;
    }
    if (n->layer_surface) {
        zwlr_layer_surface_v1_destroy(n->layer_surface);
        n->layer_surface = NULL;
    }
    if (n->surface) {
        wl_surface_destroy(n->surface);
        n->surface = NULL;
    }
    n->configured = 0;
    n->requested_h = 0;
    n->surface_h = 0;
    n->hovered_id = 0;
    if (n->display)
        wl_display_flush(n->display);
}

static void refresh_target_output(struct notify_state *n) {
    if (n->no_ui || !n->display)
        return;
    char wanted[64];
    config_output_name(n, wanted, sizeof wanted);
    DOTS_DEBUG("notify: target wanted='%s'", wanted);
    if (!wanted[0])
        return;
    struct notify_output *out = select_output(n, wanted);
    if (!out || out->wl == n->target)
        return;
    DOTS_DEBUG("notify: target switch to '%s'", out->name);
    if (n->surface)
        destroy_surface(n);
    n->target = out->wl;
}

static void reflow(struct notify_state *n) {
    if (n->no_ui || !n->display)
        return;
    if (n->count == 0) {
        destroy_surface(n);
        return;
    }

    // Urutkan tampilan: notifikasi terbaru di atas.
    int order[NOTIFY_MAX];
    int n_order = 0;
    for (int i = n->count - 1; i >= 0 && n_order < NOTIFY_MAX_VISIBLE; i--)
        order[n_order++] = i;

    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(tmp);

    int total = NOTIFY_MARGIN;
    int shown = 0;
    int max_h = 600;
    for (int i = 0; i < n->n_outputs; i++) {
        if (n->outputs[i].wl == n->target && n->outputs[i].height > 0) {
            max_h = n->outputs[i].height - NOTIFY_MARGIN * 2;
            break;
        }
    }
    if (max_h < 120)
        max_h = 120;

    for (int k = 0; k < n_order; k++) {
        struct notification *item = &n->items[order[k]];
        int h = card_height(item, cr, NOTIFY_CARD_W);
        int gap = shown > 0 ? 4 : 0;
        if (shown > 0 && total + gap + h + NOTIFY_MARGIN > max_h)
            break;
        item->visible = 1;
        item->card_h = h;
        item->card_y = total + gap;
        total += gap + h;
        shown++;
    }
    for (int i = 0; i < n->count; i++) {
        int found = 0;
        for (int k = 0; k < shown; k++) {
            if (order[k] == i) {
                found = 1;
                break;
            }
        }
        if (!found)
            n->items[i].visible = 0;
    }
    cairo_destroy(cr);
    cairo_surface_destroy(tmp);

    int height = total + NOTIFY_MARGIN;
    if (height < 1)
        height = 1;

    if (ensure_surface(n, height) < 0)
        return;
    if (height != n->requested_h && n->surface) {
        zwlr_layer_surface_v1_set_size(n->layer_surface, NOTIFY_WIDTH,
                                       (uint32_t)height);
        n->requested_h = height;
        n->surface_h = height;
        n->configured = 0;
    }
    apply_input_region(n);
    wl_surface_commit(n->surface);
    wl_display_flush(n->display);
    if (n->configured && n->buf_h == height)
        draw(n);
    else
        n->redraw = 1;
}

/* ---------------- store notifikasi ---------------- */

static void free_notification(struct notification *item) {
    if (item->icon_surface)
        cairo_surface_destroy(item->icon_surface);
    g_free(item->app_name);
    g_free(item->app_icon);
    g_free(item->summary);
    g_free(item->body);
    g_free(item->image_path);
    for (int i = 0; i < item->n_actions; i++) {
        g_free(item->actions[i].id);
        g_free(item->actions[i].label);
    }
    memset(item, 0, sizeof *item);
}

static uint32_t alloc_id(struct notify_state *n) {
    if (n->next_id == 0)
        n->next_id = 1;
    uint32_t id = n->next_id++;
    if (n->next_id == 0)
        n->next_id = 1;
    return id;
}

static void emit_closed(struct notify_state *n, uint32_t id, uint32_t reason) {
    if (!n->dbus_ready || !n->bus)
        return;
    g_dbus_connection_emit_signal(
        n->bus, NULL, "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "NotificationClosed",
        g_variant_new("(uu)", id, reason), NULL);
}

static void close_index(struct notify_state *n, int index, uint32_t reason,
                        int emit) {
    if (index < 0 || index >= n->count)
        return;
    uint32_t id = n->items[index].id;
    if (n->hovered_id == id)
        n->hovered_id = 0;
    free_notification(&n->items[index]);
    memmove(&n->items[index], &n->items[index + 1],
            (size_t)(n->count - index - 1) * sizeof n->items[0]);
    n->count--;
    if (emit)
        emit_closed(n, id, reason);
    if (n->no_ui) {
        if (n->count == 0 && !n->serve)
            g_main_loop_quit(n->loop);
    } else {
        reflow(n);
    }
}

static void close_notification(struct notify_state *n, uint32_t id,
                               uint32_t reason) {
    for (int i = 0; i < n->count; i++) {
        if (n->items[i].id == id) {
            close_index(n, i, reason, 1);
            return;
        }
    }
}

static gboolean tick_cb(gpointer data) {
    struct notify_state *n = data;
    long long now = mono_ms();
    int has_timer = 0;
    for (int i = 0; i < n->count;) {
        struct notification *item = &n->items[i];
        if (item->expire_timeout > 0) {
            has_timer = 1;
            if (!item->paused && item->expires_at > 0 &&
                now >= item->expires_at) {
                DOTS_DEBUG("notify: expire id=%u", item->id);
                close_index(n, i, 1, 1);
                continue;
            }
        }
        i++;
    }
    if (n->count == 0 || !has_timer) {
        n->tick_source = 0;
        return G_SOURCE_REMOVE;
    }
    if (n->redraw)
        draw(n);
    return G_SOURCE_CONTINUE;
}

static void ensure_tick(struct notify_state *n) {
    if (n->tick_source)
        return;
    for (int i = 0; i < n->count; i++) {
        if (n->items[i].expire_timeout > 0) {
            n->tick_source = g_timeout_add(NOTIFY_TICK_MS, tick_cb, n);
            return;
        }
    }
}

static void set_item_content(struct notification *item, const char *app_name,
                             const char *app_icon, const char *summary,
                             const char *body, const char *image_path,
                             guint8 urgency, gboolean transient,
                             GVariant *actions, gint32 expire_timeout,
                             struct notify_state *n) {
    if (item->icon_surface) {
        cairo_surface_destroy(item->icon_surface);
        item->icon_surface = NULL;
    }
    g_free(item->app_name);
    g_free(item->app_icon);
    g_free(item->summary);
    g_free(item->body);
    g_free(item->image_path);
    for (int i = 0; i < item->n_actions; i++) {
        g_free(item->actions[i].id);
        g_free(item->actions[i].label);
    }
    memset(item->actions, 0, sizeof item->actions);
    item->n_actions = 0;

    item->app_name = clean_line(app_name, NOTIFY_MAX_APP);
    if (!item->app_name[0]) {
        g_free(item->app_name);
        item->app_name = g_strdup("Application");
    }
    item->app_icon = g_strndup(app_icon ? app_icon : "", NOTIFY_MAX_ICON);
    item->summary = clean_line(summary, NOTIFY_MAX_SUMMARY);
    if (!item->summary[0]) {
        g_free(item->summary);
        item->summary = g_strdup("Notification");
    }
    item->body = clean_body(body, NOTIFY_MAX_TEXT);
    item->image_path = g_strndup(image_path ? image_path : "", NOTIFY_MAX_ICON);
    if (item->app_icon[0])
        item->icon_surface = load_png_icon(item->app_icon);
    if (!item->icon_surface && item->image_path[0])
        item->icon_surface = load_png_icon(item->image_path);
    item->urgency = urgency;
    item->transient = transient;

    if (actions) {
        GVariantIter iter;
        const char *value = NULL;
        int field = 0;
        g_variant_iter_init(&iter, actions);
        while (item->n_actions < NOTIFY_MAX_ACTIONS &&
               g_variant_iter_next(&iter, "&s", &value)) {
            if (field == 0) {
                item->actions[item->n_actions].id =
                    g_strndup(value, NOTIFY_MAX_ACTION);
            } else {
                item->actions[item->n_actions].label =
                    g_strndup(value, NOTIFY_MAX_ACTION);
                item->n_actions++;
            }
            field = !field;
        }
    }

    if (expire_timeout < 0)
        expire_timeout = n->timeout_ms;
    if (expire_timeout > 0 && expire_timeout < 200)
        expire_timeout = 200;
    item->expire_timeout = expire_timeout;
    item->expires_at = expire_timeout > 0
        ? mono_ms() + expire_timeout
        : 0;
    item->paused = 0;
    item->paused_remaining = 0;
    item->visible = 0;
    item->expanded = 0;
    item->card_h = 0;
    item->toggle_x = item->toggle_y = 0;
    item->toggle_w = item->toggle_h = 0;
    item->close_x = item->close_y = 0;
    item->close_w = item->close_h = 0;
    memset(item->action_x, 0, sizeof item->action_x);
    memset(item->action_y, 0, sizeof item->action_y);
    memset(item->action_w, 0, sizeof item->action_w);
    memset(item->action_h, 0, sizeof item->action_h);
}

static uint32_t add_notification(struct notify_state *n,
                                 const char *app_name, uint32_t replaces_id,
                                 const char *app_icon, const char *summary,
                                 const char *body, GVariant *actions,
                                 GVariant *hints, gint32 expire_timeout) {
    uint8_t urgency = 1;
    gboolean transient = FALSE;
    const char *image_path = NULL;
    if (hints) {
        g_variant_lookup(hints, "urgency", "y", &urgency);
        g_variant_lookup(hints, "transient", "b", &transient);
        g_variant_lookup(hints, "image-path", "&s", &image_path);
        if (!image_path)
            g_variant_lookup(hints, "image_path", "&s", &image_path);
    }

    struct notification *item = NULL;
    if (replaces_id) {
        item = item_by_id(n, replaces_id);
    }
    if (item) {
        uint32_t id = item->id;
        set_item_content(item, app_name, app_icon, summary, body, image_path,
                         urgency, transient, actions, expire_timeout, n);
        item->id = id;
        if (item->expanded)
            item->expanded = 0;
        if (n->hovered_id == id)
            pause_item(n, item, 1);
    } else {
        if (n->count >= NOTIFY_MAX)
            close_index(n, 0, 1, 1);
        item = &n->items[n->count++];
        memset(item, 0, sizeof *item);
        item->id = alloc_id(n);
        set_item_content(item, app_name, app_icon, summary, body, image_path,
                         urgency, transient, actions, expire_timeout, n);
    }

    ensure_tick(n);
    if (!n->no_ui) {
        refresh_target_output(n);
        reflow(n);
    }
    return item->id;
}

/* ---------------- D-Bus ---------------- */

static void handle_method_call(GDBusConnection *connection,
                               const gchar *sender,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data) {
    (void)connection; (void)sender; (void)object_path; (void)interface_name;
    struct notify_state *n = user_data;

    if (g_strcmp0(method_name, "Notify") == 0) {
        const char *app_name = NULL, *app_icon = NULL, *summary = NULL;
        const char *body = NULL;
        uint32_t replaces_id = 0;
        gint32 expire_timeout = -1;
        GVariant *actions = NULL;
        GVariant *hints = NULL;
        g_variant_get(parameters, "(&su&s&s&s@as@a{sv}i)",
                      &app_name, &replaces_id, &app_icon, &summary, &body,
                      &actions, &hints, &expire_timeout);
        uint32_t id = add_notification(n, app_name, replaces_id, app_icon,
                                       summary, body, actions, hints,
                                       expire_timeout);
        g_variant_unref(actions);
        g_variant_unref(hints);
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)", id));
        return;
    }

    if (g_strcmp0(method_name, "CloseNotification") == 0) {
        uint32_t id = 0;
        g_variant_get(parameters, "(u)", &id);
        close_notification(n, id, 3);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "GetCapabilities") == 0) {
        const char *caps[] = { "actions", "body", "icon-static", NULL };
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(^as)", caps));
        return;
    }

    if (g_strcmp0(method_name, "GetServerInformation") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(ssss)", "dots-notify",
                                      "dots-hyprland", "0.1.0", "1.2"));
        return;
    }

    g_dbus_method_invocation_return_dbus_error(
        invocation, "org.freedesktop.DBus.Error.UnknownMethod",
        "Unknown notification method");
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name,
                            gpointer user_data) {
    (void)name;
    struct notify_state *n = user_data;
    GError *error = NULL;
    n->node = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (!n->node) {
        DOTS_ERROR("notify: introspection: %s", error->message);
        g_error_free(error);
        g_main_loop_quit(n->loop);
        return;
    }
    n->bus = connection;
    n->registration = g_dbus_connection_register_object(
        connection, "/org/freedesktop/Notifications",
        n->node->interfaces[0], &interface_vtable, n, NULL, &error);
    if (!n->registration) {
        DOTS_ERROR("notify: register object: %s", error->message);
        g_error_free(error);
        g_main_loop_quit(n->loop);
        return;
    }
    DOTS_INFO("notify: D-Bus object registered");
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name,
                             gpointer user_data) {
    (void)connection;
    struct notify_state *n = user_data;
    n->dbus_ready = 1;
    DOTS_INFO("notify: acquired %s", name);
}

static void on_name_lost(GDBusConnection *connection, const gchar *name,
                         gpointer user_data) {
    (void)connection;
    struct notify_state *n = user_data;
    if (n->quitting)
        return;
    DOTS_ERROR("notify: name %s unavailable", name);
    g_main_loop_quit(n->loop);
}

/* ---------------- UI loop ---------------- */

static gboolean wayland_ready(gint fd, GIOCondition condition, gpointer data) {
    (void)fd;
    struct notify_state *n = data;
    if (condition & (G_IO_ERR | G_IO_HUP)) {
        DOTS_ERROR("notify: Wayland connection closed");
        g_main_loop_quit(n->loop);
        return G_SOURCE_REMOVE;
    }
    if (condition & G_IO_IN) {
        if (wl_display_dispatch(n->display) < 0) {
            DOTS_ERROR("notify: Wayland dispatch failed");
            g_main_loop_quit(n->loop);
            return G_SOURCE_REMOVE;
        }
    }
    wl_display_flush(n->display);
    if (n->redraw)
        draw(n);
    return G_SOURCE_CONTINUE;
}

static gboolean quit_signal(gpointer data) {
    struct notify_state *n = data;
    n->quitting = 1;
    g_main_loop_quit(n->loop);
    return G_SOURCE_REMOVE;
}

static void init_wayland(struct notify_state *n) {
    n->display = wl_display_connect(NULL);
    if (!n->display) {
        DOTS_ERROR("notify: cannot connect to Wayland");
        return;
    }
    n->registry = wl_display_get_registry(n->display);
    wl_registry_add_listener(n->registry, &registry_listener, n);
    wl_display_roundtrip(n->display);
    wl_display_roundtrip(n->display);

    if (!n->compositor || !n->shm || !n->layer_shell) {
        DOTS_ERROR("notify: missing Wayland globals");
        return;
    }

    char wanted[64];
    config_output_name(n, wanted, sizeof wanted);
    struct notify_output *out = select_output(n, wanted);
    if (!out) {
        DOTS_ERROR("notify: no output");
        return;
    }
    n->target = out->wl;

    wl_display_roundtrip(n->display);
    wl_display_flush(n->display);

    n->wayland_source = g_unix_fd_add(wl_display_get_fd(n->display),
                                      G_IO_IN | G_IO_ERR | G_IO_HUP,
                                      wayland_ready, n);
}

static void cleanup(struct notify_state *n) {
    n->quitting = 1;
    if (n->tick_source) {
        g_source_remove(n->tick_source);
        n->tick_source = 0;
    }
    if (n->wayland_source) {
        g_source_remove(n->wayland_source);
        n->wayland_source = 0;
    }
    for (int i = 0; i < n->count; i++)
        free_notification(&n->items[i]);
    n->count = 0;
    destroy_surface(n);
    if (n->pointer)
        wl_pointer_destroy(n->pointer);
    if (n->seat)
        wl_seat_destroy(n->seat);
    for (int i = 0; i < n->n_outputs; i++) {
        if (n->outputs[i].wl)
            wl_output_destroy(n->outputs[i].wl);
    }
    if (n->display)
        wl_display_disconnect(n->display);
    if (n->registration && n->bus)
        g_dbus_connection_unregister_object(n->bus, n->registration);
    if (n->node)
        g_dbus_node_info_unref(n->node);
    if (n->dbus_owner)
        g_bus_unown_name(n->dbus_owner);
    if (n->loop)
        g_main_loop_unref(n->loop);
    dots_config_destroy(&n->cfg);
}

static void usage(FILE *out) {
    fprintf(out,
        "usage: dots-notify serve [--no-ui]\n"
        "\n"
        "Server notifikasi D-Bus + popup Wayland native.\n"
        "  serve       ambil nama org.freedesktop.Notifications dan tampilkan popup\n"
        "  --no-ui     jalankan server tanpa Wayland (untuk tes D-Bus)\n"
        "\n"
        "Jangan jalankan bersamaan dengan notification server Quickshell.\n"
        "Untuk tes, gunakan session bus privat (lihat tests/test-notify.sh).\n");
}

int main(int argc, char **argv) {
    setlocale(LC_CTYPE, "");
    setlocale(LC_TIME, "");
    signal(SIGPIPE, SIG_IGN);
    dots_log_init_from_env();

    int serve = 0;
    int no_ui = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "serve") == 0) {
            serve = 1;
        } else if (strcmp(argv[i], "--no-ui") == 0) {
            no_ui = 1;
        } else {
            fprintf(stderr, "dots-notify: unknown argument: %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (!serve) {
        usage(stderr);
        return 2;
    }

    struct notify_state n;
    memset(&n, 0, sizeof n);
    n.serve = serve;
    n.no_ui = no_ui;
    n.next_id = 1;
    n.timeout_ms = NOTIFY_DEFAULT_TIMEOUT;

    dots_config_init(&n.cfg);
    if (dots_config_load(&n.cfg) == 0) {
        int timeout = dots_config_int(&n.cfg, "notifications.timeout",
                                      NOTIFY_DEFAULT_TIMEOUT);
        if (timeout > 0)
            n.timeout_ms = timeout;
    }
    if (n.timeout_ms < 200)
        n.timeout_ms = 200;
    if (n.timeout_ms > 120000)
        n.timeout_ms = 120000;
    load_theme();

    if (!getenv("DBUS_SESSION_BUS_ADDRESS") ||
        !getenv("DBUS_SESSION_BUS_ADDRESS")[0]) {
        char fallback[768];
        if (dots_runtime_dir(fallback, sizeof fallback) == 0) {
            // Ganti "/dots" dengan bus standar.
            char *slash = strrchr(fallback, '/');
            if (slash)
                *slash = '\0';
            char addr[800];
            snprintf(addr, sizeof addr, "unix:path=%s/bus", fallback);
            g_setenv("DBUS_SESSION_BUS_ADDRESS", addr, TRUE);
        }
    }

    if (!no_ui)
        init_wayland(&n);
    if (!no_ui && (!n.display || !n.target)) {
        DOTS_ERROR("notify: popup UI tidak tersedia; gunakan --no-ui untuk tes");
        cleanup(&n);
        return 1;
    }

    n.loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, quit_signal, &n);
    g_unix_signal_add(SIGTERM, quit_signal, &n);
    g_unix_signal_add(SIGHUP, quit_signal, &n);

    n.dbus_owner = g_bus_own_name(
        G_BUS_TYPE_SESSION, "org.freedesktop.Notifications",
        G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired, on_name_acquired,
        on_name_lost, &n, NULL);

    DOTS_INFO("notify: serving notifications (ui=%s)",
              no_ui ? "off" : "on");
    g_main_loop_run(n.loop);
    cleanup(&n);
    return 0;
}
