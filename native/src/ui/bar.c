// dots-bar - bar Wayland murni (C + cairo/pango), tampilan meniru
// bar Quickshell dots-hyprland (Material You, Google Sans Flex, Material Symbols).
//
// Ini adalah tahap awal migrasi: source masih membawa geometri prototipe cbar.
// Refactor menjadi layout engine dilakukan setelah baseline native berjalan.
//
// Build:
//   meson setup build native && meson compile -C native/build

#define _GNU_SOURCE
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <json-c/json.h>

#include "common/config.h"
#include "common/ipc.h"
#include "common/paths.h"

#include "wlr-layer-shell-client.h"

#define SURFACE_H 320
#define BAR_H 40
#define NWS 10
#define WS_W 26.0

#define VAR_MAIN  "wght=450,wdth=100,opsz=100"
#define VAR_TITLE "wght=500,opsz=100"
#define VAR_NUM   "wght=450"
#define VAR_HEAD  "wght=500,wdth=100,opsz=100"

struct rgba { double r, g, b, a; };
static struct rgba C_LAYER0       = { 18/255.0, 23/255.0, 20/255.0, 0.87 };
static struct rgba C_LAYER1       = { 86/255.0, 83/255.0, 90/255.0, 0.10 };
static struct rgba C_PRIMARY      = { 135/255.0, 214/255.0, 189/255.0, 1.0 };
static struct rgba C_ON_PRIMARY   = { 0/255.0, 56/255.0, 44/255.0, 1.0 };
static struct rgba C_ON_SECONDARY = { 205/255.0, 233/255.0, 222/255.0, 1.0 };
static struct rgba C_ON_LAYER0    = { 222/255.0, 228/255.0, 224/255.0, 1.0 };
static struct rgba C_LAYER0_SOLID = { 18/255.0, 23/255.0, 20/255.0, 1.0 };
static struct rgba C_ON_LAYER1    = { 191/255.0, 201/255.0, 196/255.0, 1.0 };
static struct rgba C_SUBTEXT      = { 137/255.0, 147/255.0, 142/255.0, 1.0 };
static struct rgba C_DOT          = { 133/255.0, 136/255.0, 138/255.0, 0.505 };
static struct rgba C_OCCUPIED     = { 52/255.0, 76/255.0, 68/255.0, 0.6 };
static struct rgba C_ERROR        = { 255/255.0, 180/255.0, 171/255.0, 1.0 };
static struct rgba C_SECONDARY    = { 52/255.0, 76/255.0, 68/255.0, 1.0 };
static struct rgba C_SECONDARY_HV = { 67/255.0, 92/255.0, 83/255.0, 1.0 };
static const struct rgba C_LAYER1_HOVER = { 185/255.0, 196/255.0, 190/255.0, 0.008 };
static struct rgba C_POPUP_BG     = { 27/255.0, 33/255.0, 30/255.0, 1.0 };
static struct rgba C_POPUP_BORDER = { 33/255.0, 42/255.0, 39/255.0, 1.0 };

struct state {
    // Wayland
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_output *output;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_buffer *buffer;
    void *data;
    int buf_w, buf_h;
    int out_w, out_h;
    int configured;
    int redraw;

    // Input / interactive state
    int ptr_inside;
    double ptr_x, ptr_y;
    long long last_pointer_ms;
    int bar_visible;
    double bar_y;
    unsigned hover_mask;      // bit 0 = left area, bit 1 = right area
    int lb_open, rb_open;     // sidebar toggled states, polled from Hyprland
    int popup;                // 0 none, 1 clock, 2 resources, 3 battery
    int bell_visible;         // notification indicator shown
    time_t notif_mtime;       // baseline mtime of notification history
    int notif_state_valid;    // Quickshell state bridge readable
    int notif_unread;
    int notif_silent;
    int notif_show_count;

    // To-do list (from Quickshell's todo.json)
    char todo_text[4096];
    int todo_lines;
    int todo_unfinished;
    time_t todo_mtime;
    off_t todo_size;

    // Data
    int active_ws;
    int occupied[NWS];
    char ws_class[NWS][128];
    char aw_class[128];
    char aw_title[512];
    double mem_pct;
    unsigned long mem_total_kb, mem_free_kb, mem_avail_kb;
    double swap_pct;
    unsigned long swap_total_kb, swap_free_kb;
    double cpu_pct;
    unsigned long long cpu_total, cpu_idle;
    int battery_pct;
    int battery_charging;
    int battery_full;
    double battery_power_w;
    double battery_energy_wh, battery_full_wh;
    double battery_health;
    int battery_minutes_left;
    int upower_valid;
    int upower_state;
    long long upower_time_empty;
    long long upower_time_full;
    double upower_rate;
    double upower_health;
    char clock_time[16];
    char clock_date[32];
    char clock_long_date[64];
    char uptime_str[32];
    char media_title[512];
    char media_artist[256];
    char media_status[32];
    double media_pos, media_len;

    // Resource layout / Quickshell config
    double resources_w;       // width of the Resources mouse area in the bar
    double media_x;           // first pixel of the media badge
    int cfg_loaded;
    time_t cfg_mtime;
    long cfg_mtime_ns;
    off_t cfg_size;
    int cfg_show_memory, cfg_show_cpu, cfg_show_swap;
    int cfg_always_cpu, cfg_always_swap;
    int cfg_mem_warn, cfg_cpu_warn, cfg_swap_warn;
    int cfg_battery_low;
    int cfg_auto_hide;
    int cfg_autohide_hover;
    int cfg_autohide_push;
    int cfg_bar_bottom;
    struct dots_config cfg;
    time_t theme_mtime;
    long theme_mtime_ns;
    off_t theme_size;
    int theme_loaded;

    // Icons
    cairo_surface_t *icon_kitty;
    cairo_surface_t *icon_brave;
    cairo_surface_t *icon_dolphin;
    cairo_surface_t *spark;
    cairo_surface_t *ws_icon[NWS];

    char *hypr_buf;

    time_t last_sec, last_res, last_media;
};

struct bar_layout {
    double w;
    double middle_w;
    double gx;
    double lg_group;
    double wg_group;
    double rg_group;
    double lg;
    double wg;
    double rg;
};

static struct bar_layout bar_layout_for(const struct state *s) {
    struct bar_layout l;
    l.w = s->buf_w > 0 ? s->buf_w : (s->out_w > 0 ? s->out_w : 1366);
    /* Lebar grup tengah mengikuti BarContent.qml pada bentuk penuh. */
    l.middle_w = 360 + 4 + 270 + 4 + 360;
    l.gx = floor((l.w - l.middle_w) / 2.0);
    if (l.gx < 0)
        l.gx = 0;
    l.lg_group = l.gx;
    l.wg_group = l.lg_group + 360 + 4;
    l.rg_group = l.wg_group + 270 + 4;
    l.lg = l.lg_group + 5;
    l.wg = l.wg_group + 5;
    l.rg = l.rg_group + 5;
    return l;
}

static char g_base[600] = ".";

static void init_base(void) {
    char buf[600];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return;
    buf[n] = 0;
    char *sl = strrchr(buf, '/');
    if (sl) {
        *sl = 0;
        snprintf(g_base, sizeof g_base, "%s", buf);
    }
}

static int create_shm_file(size_t size) {
    int fd = memfd_create("dots-bar", MFD_CLOEXEC);
    if (fd < 0) {
        char name[64];
        snprintf(name, sizeof name, "/dots-bar-%d", getpid());
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            shm_unlink(name);
    }
    if (fd < 0)
        return -1;
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void create_buffer(struct state *s, int w, int h) {
    if (s->buffer) {
        wl_buffer_destroy(s->buffer);
        s->buffer = NULL;
    }
    if (s->data) {
        munmap(s->data, (size_t)s->buf_w * s->buf_h * 4);
        s->data = NULL;
    }

    int stride = w * 4;
    int size = stride * h;
    int fd = create_shm_file((size_t)size);
    if (fd < 0)
        return;
    s->data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s->data == MAP_FAILED) {
        s->data = NULL;
        close(fd);
        return;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(s->shm, fd, size);
    s->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    s->buf_w = w;
    s->buf_h = h;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
}

/* ---------------- data ---------------- */

static int read_int_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1)
        v = -1;
    fclose(f);
    return v;
}

static unsigned long long read_ull_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    unsigned long long v = 0;
    if (fscanf(f, "%llu", &v) != 1)
        v = 0;
    fclose(f);
    return v;
}

static void read_upower_battery(struct state *s, const char *base) {
    const char *name = strrchr(base, '/');
    name = name ? name + 1 : base;
    char cmd[512];
    snprintf(cmd, sizeof cmd,
             "LC_ALL=C busctl --no-pager get-property org.freedesktop.UPower "
             "/org/freedesktop/UPower/devices/battery_%s "
             "org.freedesktop.UPower.Device "
             "TimeToEmpty TimeToFull EnergyRate Capacity State 2>/dev/null",
             name);
    FILE *p = popen(cmd, "r");
    if (!p)
        return;
    long long te = -1, tf = -1;
    double rate = -1, health = -1;
    int state = -1;
    char line[128];
    int got = 0;
    if (fgets(line, sizeof line, p) && sscanf(line, "%*s %lld", &te) == 1) got++;
    if (fgets(line, sizeof line, p) && sscanf(line, "%*s %lld", &tf) == 1) got++;
    if (fgets(line, sizeof line, p) && sscanf(line, "%*s %lf", &rate) == 1) got++;
    if (fgets(line, sizeof line, p) && sscanf(line, "%*s %lf", &health) == 1) got++;
    if (fgets(line, sizeof line, p) && sscanf(line, "%*s %d", &state) == 1) got++;
    pclose(p);
    if (got == 5) {
        s->upower_valid = 1;
        s->upower_time_empty = te;
        s->upower_time_full = tf;
        s->upower_rate = rate;
        s->upower_health = health;
        s->upower_state = state;
    }
}

static void read_battery(struct state *s) {
    const char *base = access("/sys/class/power_supply/BAT0/capacity", R_OK) == 0
                           ? "/sys/class/power_supply/BAT0"
                           : "/sys/class/power_supply/BAT1";
    char path[256];

    snprintf(path, sizeof path, "%s/capacity", base);
    int v = read_int_file(path);
    s->battery_pct = v < 0 ? 0 : v;

    s->battery_charging = 0;
    s->battery_full = 0;
    snprintf(path, sizeof path, "%s/status", base);
    FILE *f = fopen(path, "r");
    if (f) {
        char buf[64] = "";
        if (fgets(buf, sizeof buf, f))
            s->battery_charging = strstr(buf, "Charging") != NULL;
        if (strstr(buf, "Full"))
            s->battery_full = 1;
        fclose(f);
    }

    unsigned long long now = 0, full = 0, design = 0, power = 0;
    snprintf(path, sizeof path, "%s/energy_now", base);
    now = read_ull_file(path);
    snprintf(path, sizeof path, "%s/energy_full", base);
    full = read_ull_file(path);
    snprintf(path, sizeof path, "%s/energy_full_design", base);
    design = read_ull_file(path);
    snprintf(path, sizeof path, "%s/power_now", base);
    power = read_ull_file(path);

    // Fall back to charge/current/voltage on batteries that do not expose
    // energy directly.
    if ((now == 0 || full == 0) && now == 0) {
        unsigned long long charge = 0, charge_full = 0, charge_design = 0;
        unsigned long long current = 0, voltage = 0;
        snprintf(path, sizeof path, "%s/charge_now", base);
        charge = read_ull_file(path);
        snprintf(path, sizeof path, "%s/charge_full", base);
        charge_full = read_ull_file(path);
        snprintf(path, sizeof path, "%s/charge_full_design", base);
        charge_design = read_ull_file(path);
        snprintf(path, sizeof path, "%s/current_now", base);
        current = read_ull_file(path);
        snprintf(path, sizeof path, "%s/voltage_now", base);
        voltage = read_ull_file(path);
        if (voltage > 0) {
            now = charge * voltage / 1000000ULL;
            full = charge_full * voltage / 1000000ULL;
            design = charge_design * voltage / 1000000ULL;
            power = current * voltage / 1000000ULL;
        }
    }

    s->battery_energy_wh = now / 1000000.0;
    s->battery_full_wh = full / 1000000.0;
    s->battery_power_w = power / 1000000.0;
    s->battery_health = (design > 0)
        ? 100.0 * (double)full / (double)design : 0.0;

    s->battery_minutes_left = 0;
    if (power > 0) {
        unsigned long long remain = s->battery_charging && full > now ? full - now : now;
        double minutes = (double)remain / (double)power * 60.0;
        if (minutes > 0 && minutes < 100000)
            s->battery_minutes_left = (int)lround(minutes);
    }

    read_upower_battery(s, base);
}

static void read_mem(struct state *s) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return;
    char line[256];
    unsigned long total = 0, avail = 0, free_kb = 0;
    unsigned long swap_total = 0, swap_free = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemTotal: %lu", &total) == 1)
            continue;
        if (sscanf(line, "MemAvailable: %lu", &avail) == 1)
            continue;
        if (sscanf(line, "MemFree: %lu", &free_kb) == 1)
            continue;
        if (sscanf(line, "SwapTotal: %lu", &swap_total) == 1)
            continue;
        if (sscanf(line, "SwapFree: %lu", &swap_free) == 1)
            continue;
    }
    fclose(f);
    s->mem_total_kb = total;
    s->mem_free_kb = free_kb;
    s->mem_avail_kb = avail;
    s->swap_total_kb = swap_total;
    s->swap_free_kb = swap_free;
    if (total > 0)
        s->mem_pct = (double)(total - avail) / (double)total;
    if (swap_total > 0)
        s->swap_pct = (double)(swap_total - swap_free) / (double)swap_total;
    else
        s->swap_pct = 0;
}

static void read_cpu(struct state *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return;
    unsigned long long v[7] = {0};
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
    fclose(f);
    if (n < 4)
        return;
    unsigned long long total = 0;
    for (int i = 0; i < 7; i++)
        total += v[i];
    unsigned long long idle = v[3];
    if (s->cpu_total > 0 && total > s->cpu_total) {
        unsigned long long dt = total - s->cpu_total;
        unsigned long long di = idle - s->cpu_idle;
        s->cpu_pct = dt > 0 ? 1.0 - (double)di / (double)dt : 0;
    }
    s->cpu_total = total;
    s->cpu_idle = idle;
}

static int hypr_json(const char *cmd, char *out, size_t cap) {
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (!sig || !xdg)
        return 0;

    char path[512];
    snprintf(path, sizeof path, "%s/hypr/%s/.socket.sock", xdg, sig);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return 0;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return 0;
    }
    if (write(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return 0;
    }
    size_t off = 0;
    ssize_t n;
    while (off < cap - 1 && (n = read(fd, out + off, cap - 1 - off)) > 0)
        off += (size_t)n;
    out[off] = 0;
    close(fd);
    return 1;
}

static const char *json_key(const char *json, const char *key) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p)
        return NULL;
    p += strlen(pat);
    while (*p && *p != ':')
        p++;
    if (*p == ':')
        p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static int json_string(const char *json, const char *key, char *out, size_t cap) {
    const char *p = json_key(json, key);
    if (!p || *p != '"')
        return 0;
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') out[o++] = '\n';
            else if (*p == 't') out[o++] = '\t';
            else if (*p == 'r') out[o++] = '\r';
            else if (*p == 'u') {
                p += 4;
            } else if (*p) out[o++] = *p;
        } else {
            out[o++] = *p;
        }
        if (*p)
            p++;
    }
    out[o] = 0;
    return 1;
}

static int json_int(const char *json, const char *key) {
    const char *p = json_key(json, key);
    return p ? atoi(p) : 0;
}

static void parse_workspaces(struct state *s, const char *json) {
    memset(s->occupied, 0, sizeof s->occupied);
    const char *p = json;
    while ((p = strstr(p, "\"id\""))) {
        const char *q = p + 4;
        while (*q && *q != ':')
            q++;
        if (*q == ':')
            q++;
        int id = atoi(q);
        if (id >= 1 && id <= NWS)
            s->occupied[id - 1] = 1;
        p = q;
    }
}

static void parse_clients(struct state *s, const char *json) {
    char classes[NWS][128];
    memset(classes, 0, sizeof classes);

    const char *p = json;
    while ((p = strstr(p, "\"address\""))) {
        const char *next = strstr(p + 9, "\"address\"");
        const char *end = next ? next : json + strlen(json);

        int wsid = 0;
        const char *w = strstr(p, "\"workspace\"");
        if (w && w < end) {
            const char *q = w;
            while (q < end && strncmp(q, "\"id\"", 4) != 0)
                q++;
            if (q < end) {
                q += 4;
                while (q < end && *q != ':')
                    q++;
                if (q < end)
                    wsid = atoi(q + 1);
            }
        }
        char cls[128] = "";
        const char *c = strstr(p, "\"class\"");
        if (c && c < end) {
            char *tmp = strndup(c, (size_t)(end - c));
            if (tmp) {
                json_string(tmp, "class", cls, sizeof cls);
                free(tmp);
            }
        }
        if (wsid >= 1 && wsid <= NWS && cls[0] && !classes[wsid - 1][0])
            snprintf(classes[wsid - 1], sizeof classes[wsid - 1], "%s", cls);

        if (!next)
            break;
        p = next;
    }

    for (int i = 0; i < NWS; i++) {
        if (strcmp(classes[i], s->ws_class[i]) != 0) {
            snprintf(s->ws_class[i], sizeof s->ws_class[i], "%s", classes[i]);
            if (strstr(classes[i], "kitty")) s->ws_icon[i] = s->icon_kitty;
            else if (strstr(classes[i], "brave")) s->ws_icon[i] = s->icon_brave;
            else if (strstr(classes[i], "dolphin")) s->ws_icon[i] = s->icon_dolphin;
            else s->ws_icon[i] = NULL;
        }
    }
}

static void refresh_clock(struct state *s) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(s->clock_time, sizeof s->clock_time, "%H:%M", &tm);
    strftime(s->clock_date, sizeof s->clock_date, "%a, %d/%m", &tm);
    strftime(s->clock_long_date, sizeof s->clock_long_date,
             "%A, %B %d, %Y", &tm);

    s->uptime_str[0] = 0;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
        double up = 0;
        if (fscanf(f, "%lf", &up) == 1) {
            int total = (int)(up / 60.0);
            int days = total / (24 * 60);
            int hours = (total / 60) % 24;
            int mins = total % 60;
            size_t off = 0;
            s->uptime_str[0] = 0;
            if (days > 0)
                off += (size_t)snprintf(s->uptime_str + off,
                                        sizeof s->uptime_str - off, "%dd", days);
            if (hours > 0)
                off += (size_t)snprintf(s->uptime_str + off,
                                        sizeof s->uptime_str - off, "%s%dh",
                                        off ? ", " : "", hours);
            if (mins > 0 || off == 0)
                snprintf(s->uptime_str + off, sizeof s->uptime_str - off,
                         "%s%dm", off ? ", " : "", mins);
        }
        fclose(f);
    }
}

static void refresh_hypr(struct state *s) {
    if (!s->hypr_buf)
        return;
    size_t cap = 1024 * 512;

    if (hypr_json("j/activeworkspace", s->hypr_buf, cap))
        s->active_ws = json_int(s->hypr_buf, "id");
    if (hypr_json("j/workspaces", s->hypr_buf, cap))
        parse_workspaces(s, s->hypr_buf);
    if (hypr_json("j/clients", s->hypr_buf, cap))
        parse_clients(s, s->hypr_buf);
    if (hypr_json("j/activewindow", s->hypr_buf, cap)) {
        json_string(s->hypr_buf, "class", s->aw_class, sizeof s->aw_class);
        json_string(s->hypr_buf, "title", s->aw_title, sizeof s->aw_title);
    }
}

static time_t notif_mtime_now(void);
static void read_notif_state(struct state *s);
static void read_config(struct state *s);
static void load_todos(struct state *s);

static void refresh_sidebars(struct state *s) {
    read_config(s);
    read_notif_state(s);
    load_todos(s);
    if (!s->hypr_buf)
        return;
    if (!hypr_json("j/layers", s->hypr_buf, 1024 * 512))
        return;
    int l = strstr(s->hypr_buf, "quickshell:sidebarLeft") != NULL;
    int r = strstr(s->hypr_buf, "quickshell:sidebarRight") != NULL;
    if (l != s->lb_open || r != s->rb_open) {
        s->lb_open = l;
        s->rb_open = r;
        s->redraw = 1;
    }

    // Notification bell. Prefer the explicit state bridge written by
    // Notifications.qml (unread count, silent/DND, showUnreadCount). Fall
    // back to the history file mtime for setups without the bridge.
    if (s->notif_state_valid) {
        int vis = s->notif_silent || s->notif_unread > 0;
        // Opening the right sidebar marks everything read; silent still shows
        // the paused bell, exactly like Notifications.silent || unread > 0.
        if (s->rb_open && !s->notif_silent)
            vis = 0;
        if (vis != s->bell_visible) {
            s->bell_visible = vis;
            s->redraw = 1;
        }
        s->notif_mtime = notif_mtime_now();
    } else {
        time_t mt = notif_mtime_now();
        if (s->rb_open) {
            if (s->bell_visible) {
                s->bell_visible = 0;
                s->redraw = 1;
            }
            s->notif_mtime = mt;
        } else if (s->notif_mtime == 0) {
            s->notif_mtime = mt;
        } else if (mt != s->notif_mtime) {
            s->notif_mtime = mt;
            s->bell_visible = 1;
            s->notif_unread = 1;
            s->notif_silent = 0;
            s->redraw = 1;
        }
    }
}

/* Fire-and-forget child process (no shell, fixed argv). */
static void spawn_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
}

static int call_daemon_panel(const char *target, const char *fn) {
    char sock[1024];
    if (dots_socket_path(sock, sizeof sock) < 0)
        return -1;
    int fd = dots_ipc_client_connect(sock);
    if (fd < 0)
        return -1;

    char req[2048];
    int n = snprintf(req, sizeof req,
                     "{\"id\":1,\"cmd\":\"panel\",\"target\":\"%s\",\"action\":\"%s\"}",
                     target, fn);
    if (n < 0 || (size_t)n >= sizeof req) {
        close(fd);
        return -1;
    }
    int ok = -1;
    if (dots_ipc_send_line(fd, req) == 0) {
        char response[DOTS_IPC_MAX];
        if (dots_ipc_recv_line(fd, response, sizeof response) > 0 &&
            strstr(response, "\"ok\":true"))
            ok = 0;
    }
    close(fd);
    return ok;
}

static void call_qs(const char *target, const char *fn) {
    // Transisi: dotsd mendelegasikan ke Quickshell. Jika daemon belum ada,
    // panggil qs langsung supaya bar tetap berfungsi.
    if (call_daemon_panel(target, fn) == 0)
        return;

    const char *config = getenv("DOTS_QS_CONFIG");
    if (!config || !config[0])
        config = "ii";
    char *argv[] = { "qs", "-c", (char *)config, "ipc", "call",
                     (char *)target, (char *)fn, NULL };
    spawn_cmd(argv);
}

static void toggle_sidebar(struct state *s, int right) {
    if (right) {
        s->rb_open = !s->rb_open;
        if (s->rb_open) {
            if (!s->notif_silent) {
                s->bell_visible = 0;
                s->notif_unread = 0;
            }
            s->notif_mtime = notif_mtime_now();
        }
        call_qs("sidebarRight", "toggle");
    } else {
        s->lb_open = !s->lb_open;
        call_qs("sidebarLeft", "toggle");
    }
    s->redraw = 1;
}

static void focus_workspace(struct state *s, int ws) {
    char expr[96];
    snprintf(expr, sizeof expr, "hl.dsp.focus({workspace = %d})", ws);
    char *argv[] = { "hyprctl", "dispatch", expr, NULL };
    spawn_cmd(argv);
    s->active_ws = ws;
    s->redraw = 1;
}

static void focus_workspace_rel(int delta) {
    char *argv[] = { "hyprctl", "dispatch",
                     delta > 0 ? "hl.dsp.focus({workspace = \"r+1\"})"
                               : "hl.dsp.focus({workspace = \"r-1\"})",
                     NULL };
    spawn_cmd(argv);
}

static void toggle_special_workspace(void) {
    char *argv[] = { "hyprctl", "dispatch",
                     "hl.dsp.workspace.toggle_special(\"special\")", NULL };
    spawn_cmd(argv);
}

static void change_volume(int up) {
    char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                     up ? "2%+" : "2%-", NULL };
    spawn_cmd(argv);
}

static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void apply_layer_state(struct state *s) {
    if (!s->surface)
        return;

    double w = s->buf_w > 0 ? s->buf_w : (s->out_w > 0 ? s->out_w : 1366);
    int input_h = (s->cfg_auto_hide && !s->bar_visible)
        ? s->cfg_autohide_hover : BAR_H;
    // Saat bar bawah tersembunyi, dock bisa menutupi tepi bawah layar. Pakai
    // tepi atas bar (yang masih di atas dock) sebagai hover strip.
    int input_y = (int)s->bar_y;
    struct wl_region *region = wl_compositor_create_region(s->compositor);
    if (region) {
        wl_region_add(region, 0, input_y, (int32_t)w, input_h);
        // Bar bawah juga bisa dipicu dari tepi bawah surface. Kalau dock
        // menutupinya, strip di tepi atas bar tetap bisa dijangkau.
        if (s->cfg_auto_hide && !s->bar_visible && s->cfg_bar_bottom)
            wl_region_add(region, 0, SURFACE_H - input_h, (int32_t)w, input_h);
        wl_surface_set_input_region(s->surface, region);
        wl_region_destroy(region);
    }

    if (s->layer_surface) {
        int zone = 40;
        if (s->cfg_auto_hide &&
            (!s->bar_visible || !s->cfg_autohide_push))
            zone = 0;
        zwlr_layer_surface_v1_set_exclusive_zone(s->layer_surface, zone);
    }
    s->redraw = 1;
}

static void show_bar(struct state *s) {
    s->last_pointer_ms = mono_ms();
    if (s->bar_visible)
        return;
    s->bar_visible = 1;
    apply_layer_state(s);
}

static void hide_bar(struct state *s) {
    if (!s->cfg_auto_hide || s->popup || s->ptr_inside)
        return;
    if (!s->bar_visible)
        return;
    s->bar_visible = 0;
    s->hover_mask = 0;
    apply_layer_state(s);
}

static void handle_click(struct state *s, uint32_t button, double x, double y) {
    if (y < 0 || y >= BAR_H)
        return;

    struct bar_layout l = bar_layout_for(s);

    // Left section (sidebar button + active window): sidebarLeft.
    if (x < l.gx) {
        if (button == BTN_LEFT)
            toggle_sidebar(s, 0);
        return;
    }
    // Workspaces: click focuses a workspace, right click opens overview,
    // side/back button toggles the special workspace.
    if (x >= l.wg_group && x < l.wg_group + 270) {
        if (button == BTN_LEFT) {
            int idx = (int)((x - l.wg) / WS_W);
            if (idx < 0) idx = 0;
            if (idx >= NWS) idx = NWS - 1;
            focus_workspace(s, idx + 1);
        } else if (button == BTN_RIGHT) {
            call_qs("search", "workspacesToggle");
        } else if (button == BTN_BACK || button == BTN_SIDE) {
            toggle_special_workspace();
        }
        return;
    }
    // Media widget. Its left edge follows the Resources group.
    if (x >= s->media_x + 5 && x < l.lg_group + 355) {
        if (button == BTN_LEFT)
            call_qs("mediaControls", "toggle");
        else if (button == BTN_MIDDLE)
            call_qs("mpris", "playPause");
        else if (button == BTN_RIGHT || button == BTN_FORWARD ||
                 button == BTN_EXTRA)
            call_qs("mpris", "next");
        else if (button == BTN_BACK || button == BTN_SIDE)
            call_qs("mpris", "previous");
        return;
    }
    // Resources: no click actions in the QML.
    if (x >= l.gx && x < l.wg_group)
        return;

    // Clock/battery/right section: sidebarRight.
    if (button == BTN_LEFT)
        toggle_sidebar(s, 1);
}

static void handle_scroll(struct state *s, double x, double y, double dy) {
    if (y < 0 || y >= BAR_H || dy == 0)
        return;
    struct bar_layout l = bar_layout_for(s);
    if (x < l.gx) {
        call_qs("brightness", dy < 0 ? "increment" : "decrement");
    } else if (x >= l.wg_group && x < l.wg_group + 270) {
        // Scroll down/up on workspaces: next/previous relative workspace.
        focus_workspace_rel(dy > 0 ? 1 : -1);
    } else if (x >= l.rg_group + 360) {
        change_volume(dy < 0 ? 1 : 0);
    }
}

static void update_hover(struct state *s) {
    // Screenshot/testing hook; normal input is unaffected when unset.
    const char *forced = getenv("CPROTO_FORCE_POPUP");
    if (forced) {
        int p = atoi(forced);
        if (p != s->popup) {
            s->popup = p;
            s->redraw = 1;
        }
        show_bar(s);
        return;
    }

    unsigned mask = 0;
    int popup = 0;
    double yy = s->ptr_y - s->bar_y;
    if (s->bar_visible && s->ptr_inside && yy >= 0 && yy < BAR_H) {
        struct bar_layout l = bar_layout_for(s);
        double x = s->ptr_x;
        if (x < l.gx)
            mask |= 1;
        else if (x >= l.rg_group + 360)
            mask |= 2;

        if (x >= l.gx && s->resources_w > 8 &&
            x < l.gx + s->resources_w + 9)
            popup = 2;      // resources
        else if (x >= l.rg_group + 318 && x < l.rg_group + 360)
            popup = 3;      // battery
        else if (x >= l.gx + 721 && x < l.gx + 866)
            popup = 1;      // clock
    }
    if (mask != s->hover_mask || popup != s->popup) {
        s->hover_mask = mask;
        s->popup = popup;
        s->redraw = 1;
    }
}

static void refresh_media(struct state *s) {
    // Ask every MPRIS player, then prefer the one that is actually playing.
    // Quickshell tracks the active player; `playerctl metadata` alone picks an
    // arbitrary (often stopped) player and shows the wrong track.
    const char *cmd =
        "playerctl -a metadata --format "
        "'{{status}}|{{title}}|{{artist}}|{{position}}|{{mpris:length}}' 2>/dev/null";
    FILE *p = popen(cmd, "r");
    if (!p)
        return;

    char best[2048] = "";
    int best_rank = -1;
    char line[2048];
    while (fgets(line, sizeof line, p)) {
        line[strcspn(line, "\n")] = 0;
        int rank = 0; // Stopped
        if (strncmp(line, "Playing|", 8) == 0)
            rank = 2;
        else if (strncmp(line, "Paused|", 7) == 0)
            rank = 1;
        if (rank > best_rank) {
            best_rank = rank;
            snprintf(best, sizeof best, "%s", line);
        }
    }
    pclose(p);
    if (!best[0])
        return;

    char title[512] = "", artist[256] = "", status[32] = "";
    double pos = 0, len = 0;
    char *save = NULL;
    char *tok = strtok_r(best, "|", &save);
    if (tok) snprintf(status, sizeof status, "%s", tok);
    tok = strtok_r(NULL, "|", &save);
    if (tok) snprintf(title, sizeof title, "%s", tok);
    tok = strtok_r(NULL, "|", &save);
    if (tok) snprintf(artist, sizeof artist, "%s", tok);
    tok = strtok_r(NULL, "|", &save);
    if (tok) pos = atof(tok);
    tok = strtok_r(NULL, "|", &save);
    if (tok) len = atof(tok);

    snprintf(s->media_title, sizeof s->media_title, "%s", title);
    snprintf(s->media_artist, sizeof s->media_artist, "%s", artist);
    snprintf(s->media_status, sizeof s->media_status, "%s", status);
    s->media_pos = pos;
    s->media_len = len;
}

/* ---------------- drawing ---------------- */

static PangoLayout *make_layout(cairo_t *cr, const char *text, double px,
                                const char *variations, double max_w) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, "Google Sans Flex");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    if (variations)
        pango_font_description_set_variations(d, variations);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, text, -1);
    pango_layout_set_single_paragraph_mode(l, TRUE);
    if (max_w > 0) {
        pango_layout_set_width(l, (int)(max_w * PANGO_SCALE));
        pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    }
    return l;
}

static PangoLayout *make_layout_multi(cairo_t *cr, const char *text, double px,
                                      const char *variations) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, "Google Sans Flex");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    if (variations)
        pango_font_description_set_variations(d, variations);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, text, -1);
    // Qt's NativeRendering line height for the 15px todo text is 19px, not
    // Pango's 20px. The native font advances are also slightly wider, so add
    // 0.2px letter spacing to keep long task cards at the real width.
    pango_layout_set_spacing(l, -PANGO_SCALE);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs,
        pango_attr_letter_spacing_new((int)lround(0.2 * PANGO_SCALE)));
    pango_layout_set_attributes(l, attrs);
    pango_attr_list_unref(attrs);
    return l;
}

static void draw_text(cairo_t *cr, const char *text, double px, const char *var,
                      double max_w, double x, double top, struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px, var, max_w);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, x, top);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_mid(cairo_t *cr, const char *text, double px, const char *var,
                          double max_w, double x, double cy, struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px, var, max_w);
    int w, h;
    PangoRectangle ink;
    pango_layout_get_pixel_size(l, &w, &h);
    pango_layout_get_pixel_extents(l, &ink, NULL);
    (void)w;
    /* Qt centers the elided text inside its item; center the visible ink. */
    double dx = x + (max_w - ink.width) / 2.0 - ink.x;
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, dx, cy - h / 2.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_centered(cairo_t *cr, const char *text, double px, const char *var,
                                double cx, double cy, struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px, var, 0);
    int w, h;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static PangoLayout *make_layout_sp(cairo_t *cr, const char *text, double px,
                                  const char *var, double spacing) {
    PangoLayout *l = make_layout(cr, text, px, var, 0);
    if (spacing != 0.0) {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs,
            pango_attr_letter_spacing_new((int)lround(spacing * PANGO_SCALE)));
        pango_layout_set_attributes(l, attrs);
        pango_attr_list_unref(attrs);
    }
    return l;
}

static void draw_text_vmid_left_sp(cairo_t *cr, const char *text, double px,
                                   const char *var, double x, double cy,
                                   struct rgba col, double spacing) {
    PangoLayout *l = make_layout_sp(cr, text, px, var, spacing);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(l, &ink, NULL);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, x - ink.x, cy - (ink.y + ink.height / 2.0));
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_vmid_right_sp(cairo_t *cr, const char *text, double px,
                                    const char *var, double right, double cy,
                                    struct rgba col, double spacing) {
    PangoLayout *l = make_layout_sp(cr, text, px, var, spacing);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(l, &ink, NULL);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, right - ink.x - ink.width,
                  cy - (ink.y + ink.height / 2.0));
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_vmid_left(cairo_t *cr, const char *text, double px,
                                const char *var, double x, double cy,
                                struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px, var, 0);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(l, &ink, NULL);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, x - ink.x, cy - (ink.y + ink.height / 2.0));
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_vmid_right(cairo_t *cr, const char *text, double px,
                                 const char *var, double right, double cy,
                                 struct rgba col) {
    PangoLayout *l = make_layout(cr, text, px, var, 0);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(l, &ink, NULL);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, right - ink.x - ink.width,
                  cy - (ink.y + ink.height / 2.0));
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static void draw_text_top_left(cairo_t *cr, const char *text, double px,
                               const char *var, double x, double y,
                               struct rgba col) {
    PangoLayout *l = make_layout_multi(cr, text, px, var);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(l, &ink, NULL);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    // Keep the layout origin: the to-do lines start with two spaces that must
    // remain visible, exactly like the real StyledText item at the content x.
    cairo_move_to(cr, x, y - ink.y);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static PangoLayout *make_icon(cairo_t *cr, const char *name, double px, int fill) {
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, "Material Symbols Rounded");
    pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
    pango_font_description_set_weight(d, (fill == 1 || fill == 2)
                                         ? PANGO_WEIGHT_SEMIBOLD
                                         : PANGO_WEIGHT_NORMAL);
    char var[64];
    snprintf(var, sizeof var, "FILL=%.0f,opsz=%.0f", fill == 1 ? 1.0 : 0.0, px);
    pango_font_description_set_variations(d, var);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    pango_layout_set_text(l, name, -1);
    return l;
}

static void draw_icon_mid(cairo_t *cr, const char *name, double px, int fill,
                          double cx, double cy, struct rgba col) {
    PangoLayout *l = make_icon(cr, name, px, fill);
    int w, h;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_source_rgba(cr, col.r, col.g, col.b, col.a);
    cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
}

static double text_width(cairo_t *cr, const char *text, double px, const char *var) {
    PangoLayout *l = make_layout(cr, text, px, var, 0);
    int w, h;
    pango_layout_get_pixel_size(l, &w, &h);
    g_object_unref(l);
    return w;
}

/* Width of the widest line. pango_layout_get_pixel_size() on some Pango
 * versions reports the sum of all lines, which makes multiline popups
 * stretch off-screen. */
static double text_width_max_line(cairo_t *cr, const char *text,
                                  double px, const char *var) {
    PangoLayout *l = make_layout_multi(cr, text, px, var);
    double maxw = 0;
    int lines = pango_layout_get_line_count(l);
    for (int i = 0; i < lines; i++) {
        PangoLayoutLine *line = pango_layout_get_line(l, i);
        PangoRectangle logical;
        pango_layout_line_get_pixel_extents(line, NULL, &logical);
        double w = logical.width;
        if (w > maxw)
            maxw = w;
    }
    g_object_unref(l);
    return maxw;
}

static cairo_surface_t *load_png(const char *name) {
    char path[700];
    const char *asset_dir = getenv("DOTS_ASSET_DIR");
    snprintf(path, sizeof path, "%s/%s",
             (asset_dir && asset_dir[0]) ? asset_dir : g_base, name);
    cairo_surface_t *s = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return NULL;
    }
    return s;
}

static unsigned char clamp255(double v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (unsigned char)(v + 0.5);
}

static void colorize_surface(cairo_surface_t *s, struct rgba tint, double amount) {
    if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS)
        return;
    cairo_surface_flush(s);
    int w = cairo_image_surface_get_width(s);
    int h = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    unsigned char *data = cairo_image_surface_get_data(s);
    for (int y = 0; y < h; y++) {
        unsigned char *row = data + y * stride;
        for (int x = 0; x < w; x++) {
            unsigned char *p = row + x * 4;
            unsigned int a = p[3];
            if (!a)
                continue;
            double r = p[2] * 255.0 / a;
            double g = p[1] * 255.0 / a;
            double b = p[0] * 255.0 / a;
            double luma = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
            r = r * (1.0 - amount) + tint.r * 255.0 * luma * amount;
            g = g * (1.0 - amount) + tint.g * 255.0 * luma * amount;
            b = b * (1.0 - amount) + tint.b * 255.0 * luma * amount;
            p[2] = clamp255(r * a / 255.0);
            p[1] = clamp255(g * a / 255.0);
            p[0] = clamp255(b * a / 255.0);
        }
    }
    cairo_surface_mark_dirty(s);
}

static void draw_app_icon(cairo_t *cr, cairo_surface_t *img,
                          double x, double y, double size) {
    if (!img)
        return;
    int iw = cairo_image_surface_get_width(img);
    int ih = cairo_image_surface_get_height(img);
    int S = (int)lround(size);
    if (iw <= 0 || ih <= 0 || S <= 0)
        return;
    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, S, S);
    cairo_t *tc = cairo_create(tmp);
    cairo_scale(tc, (double)S / iw, (double)S / ih);
    cairo_set_source_surface(tc, img, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(tc), CAIRO_FILTER_BEST);
    cairo_paint(tc);
    cairo_destroy(tc);
    colorize_surface(tmp, C_ON_SECONDARY, 0.5);
    cairo_save(cr);
    cairo_arc(cr, x + size / 2.0, y + size / 2.0, size / 2.0, 0, 2 * M_PI);
    cairo_clip(cr);
    cairo_set_source_surface(cr, tmp, x, y);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(tmp);
}

static void draw_image_circle(cairo_t *cr, cairo_surface_t *img,
                              double x, double y, double size) {
    if (!img)
        return;
    int iw = cairo_image_surface_get_width(img);
    int ih = cairo_image_surface_get_height(img);
    if (iw <= 0 || ih <= 0)
        return;
    cairo_save(cr);
    cairo_arc(cr, x + size / 2.0, y + size / 2.0, size / 2.0, 0, 2 * M_PI);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size / iw, size / ih);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void draw_badge(cairo_t *cr, double cx, double cy, double value,
                       const char *icon, int error) {
    struct rgba fill = error ? C_ERROR : C_ON_SECONDARY;
    cairo_push_group(cr);

    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 0.5);
    cairo_arc(cr, cx, cy, 10, 0, 2 * M_PI);
    cairo_fill(cr);

    if (value > 0.001) {
        cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 1.0);
        cairo_move_to(cr, cx, cy);
        cairo_arc(cr, cx, cy, 9, -M_PI_2, -M_PI_2 + 2 * M_PI * value);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_set_line_width(cr, 2);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_arc(cr, cx, cy, 9, -M_PI_2, -M_PI_2 + 2 * M_PI * value);
        cairo_stroke(cr);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    draw_icon_mid(cr, icon, 16, 1, cx, cy, (struct rgba){0, 0, 0, 1});
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}

static void draw_battery(cairo_t *cr, double x, double y, double pct,
                         int charging, int low) {
    if (getenv("CPROTO_FORCE_CHARGING"))
        charging = 1;
    struct rgba fill = (low && !charging) ? C_ERROR : C_ON_SECONDARY;
    cairo_push_group(cr);

    rounded_rect(cr, x, y, 30, 18, 9);
    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 0.5);
    cairo_fill(cr);

    cairo_save(cr);
    rounded_rect(cr, x, y, 30, 18, 9);
    cairo_clip(cr);
    cairo_rectangle(cr, x, y, 30 * pct, 18);
    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 1.0);
    cairo_fill(cr);
    cairo_restore(cr);

    char text[8];
    snprintf(text, sizeof text, "%d", (int)lround(pct * 100));
    PangoLayout *l = make_layout(cr, text, 13, "wght=600,opsz=100", 0);
    int w, h;
    pango_layout_get_pixel_size(l, &w, &h);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    if (charging && pct < 0.999) {
        double row_w = 8 + w; // 12px bolt glyph with -2px side margins
        double row_l = x + (30 - row_w) / 2.0;
        draw_icon_mid(cr, "bolt", 12, 1, row_l + 4, y + 9,
                      (struct rgba){0, 0, 0, 1});
        cairo_move_to(cr, row_l + 8, y + 9 - h / 2.0);
    } else {
        cairo_move_to(cr, x + 15 - w / 2.0, y + 9 - h / 2.0);
    }
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}

static void draw_popup_card(cairo_t *cr, double x, double y,
                            double w, double h) {
    for (int i = 6; i >= 1; i--) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.03);
        rounded_rect(cr, x - i, y - i + 2, w + 2 * i, h + 2 * i, 12 + i);
        cairo_fill(cr);
    }
    cairo_set_source_rgba(cr, C_POPUP_BORDER.r, C_POPUP_BORDER.g,
                          C_POPUP_BORDER.b, C_POPUP_BORDER.a);
    rounded_rect(cr, x, y, w, h, 12);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, C_POPUP_BG.r, C_POPUP_BG.g, C_POPUP_BG.b,
                          C_POPUP_BG.a);
    rounded_rect(cr, x + 1, y + 1, w - 2, h - 2, 11);
    cairo_fill(cr);
}

static void fmt_dot1(char *out, size_t n, double v) {
    long long q = llround(v * 10.0);
    snprintf(out, n, "%lld.%01lld", q / 10, llabs(q % 10));
}

static void fmt_dot2(char *out, size_t n, double v) {
    long long q = llround(v * 100.0);
    snprintf(out, n, "%lld.%02lld", q / 100, llabs(q % 100));
}

static void popup_header(cairo_t *cr, double x, double cy,
                         const char *icon, const char *label) {
    draw_icon_mid(cr, icon, 17, 2, x + 8.5, cy, C_ON_LAYER1);
    draw_text_vmid_left(cr, label, 15.7, VAR_HEAD, x + 22, cy, C_ON_LAYER1);
}

static void popup_row(cairo_t *cr, double x, double right, double cy,
                      const char *icon, const char *label, const char *value) {
    if (icon)
        draw_icon_mid(cr, icon, 17, 0, x + 8.5, cy, C_ON_LAYER1);
    draw_text_vmid_left_sp(cr, label, 15, VAR_MAIN, x + 21, cy, C_ON_LAYER1, -0.15);
    if (value && value[0])
        draw_text_vmid_right_sp(cr, value, 15, VAR_MAIN, right, cy, C_ON_LAYER1, -0.15);
}

static void popup_row_follow(cairo_t *cr, double x, double cy,
                             const char *icon, const char *label,
                             const char *value) {
    if (icon)
        draw_icon_mid(cr, icon, 17, 0, x + 8.5, cy, C_ON_LAYER1);
    double lx = x + 21;
    draw_text_vmid_left_sp(cr, label, 15, VAR_MAIN, lx, cy, C_ON_LAYER1, -0.15);
    if (value && value[0]) {
        PangoLayout *l = make_layout_sp(cr, label, 15, VAR_MAIN, -0.15);
        int lw, lh;
        pango_layout_get_pixel_size(l, &lw, &lh);
        g_object_unref(l);
        draw_text_vmid_left_sp(cr, value, 15, VAR_MAIN, lx + lw + 4, cy,
                               C_ON_LAYER1, -0.15);
    }
}

static void draw_popups(struct state *s, cairo_t *cr, int above) {
    if (s->popup == 0)
        return;
    char v1[64], v2[64], v3[64];

    if (s->popup == 2) { // resources
        double total_gb = s->mem_total_kb / 1048576.0;
        double free_gb = s->mem_avail_kb / 1048576.0;
        double used_gb = total_gb - free_gb;
        char gb[64];
        fmt_dot1(gb, sizeof gb, used_gb);
        snprintf(v1, sizeof v1, "%s GB", gb);
        fmt_dot1(gb, sizeof gb, free_gb);
        snprintf(v2, sizeof v2, "%s GB", gb);
        fmt_dot1(gb, sizeof gb, total_gb);
        snprintf(v3, sizeof v3, "%s GB", gb);
        char cpu_s[32];
        snprintf(cpu_s, sizeof cpu_s, "%d%%", (int)lround(s->cpu_pct * 100));

        double ram_hdr = 17 + 5 + text_width(cr, "RAM", 15.7, VAR_HEAD);
        double ram_used = 17 + 4 + text_width(cr, "Digunakan:", 15, VAR_MAIN) +
                          4 + text_width(cr, v1, 15, VAR_MAIN);
        double ram_free = 17 + 4 + text_width(cr, "Gratis:", 15, VAR_MAIN) +
                          4 + text_width(cr, v2, 15, VAR_MAIN);
        double ram_total = 17 + 4 + text_width(cr, "Total:", 15, VAR_MAIN) +
                           4 + text_width(cr, v3, 15, VAR_MAIN);
        double cpu_hdr = 17 + 5 + text_width(cr, "CPU", 15.7, VAR_HEAD);
        double cpu_load = 17 + 4 + text_width(cr, "Beban:", 15, VAR_MAIN) +
                          4 + text_width(cr, cpu_s, 15, VAR_MAIN);
        int show_mem = s->cfg_show_memory;
        int show_swap = s->cfg_show_swap && s->swap_total_kb > 0;
        int show_cpu = s->cfg_show_cpu;
        if (!show_mem && !show_swap && !show_cpu)
            return;

        double ram_w = fmax(ram_hdr, fmax(ram_used, fmax(ram_free, ram_total)));
        double swap_w = 0, cpu_w = fmax(cpu_hdr, cpu_load);
        char sw1[64] = "", sw2[64] = "", sw3[64] = "";
        if (show_swap) {
            double st_gb = s->swap_total_kb / 1048576.0;
            double sf_gb = s->swap_free_kb / 1048576.0;
            double su_gb = st_gb - sf_gb;
            char gb[64];
            fmt_dot1(gb, sizeof gb, su_gb);
            snprintf(sw1, sizeof sw1, "%s GB", gb);
            fmt_dot1(gb, sizeof gb, sf_gb);
            snprintf(sw2, sizeof sw2, "%s GB", gb);
            fmt_dot1(gb, sizeof gb, st_gb);
            snprintf(sw3, sizeof sw3, "%s GB", gb);
            double sh = 17 + 5 + text_width(cr, "Swap", 15.7, VAR_HEAD);
            double su = 17 + 4 + text_width(cr, "Digunakan:", 15, VAR_MAIN) +
                        4 + text_width(cr, sw1, 15, VAR_MAIN);
            double sf = 17 + 4 + text_width(cr, "Gratis:", 15, VAR_MAIN) +
                        4 + text_width(cr, sw2, 15, VAR_MAIN);
            double st = 17 + 4 + text_width(cr, "Total:", 15, VAR_MAIN) +
                        4 + text_width(cr, sw3, 15, VAR_MAIN);
            swap_w = fmax(sh, fmax(su, fmax(sf, st)));
        }

        double content_w = 0;
        int ncol = 0;
        if (show_mem) {
            content_w += ram_w;
            ncol++;
        }
        if (show_swap) {
            if (ncol)
                content_w += 12;
            content_w += swap_w;
            ncol++;
        }
        if (show_cpu) {
            if (ncol)
                content_w += 12;
            content_w += cpu_w;
            ncol++;
        }
        double w = content_w + 22;
        // The RAM/Swap columns have three rows; a CPU-only card is shorter.
        double h = (show_mem || show_swap) ? 120 : 70;
        struct bar_layout l = bar_layout_for(s);
        double center = l.gx + 70.5 + (s->resources_w - 110.0) / 2.0;
        double x = center - w / 2.0;
        double y = above ? -50.0 - h : 50.0;
        draw_popup_card(cr, x, y, w, h);
        double cx = x + 10;
        if (show_mem) {
            popup_header(cr, cx, y + 19.5, "memory", "RAM");
            popup_row_follow(cr, cx, y + 49.5, "clock_loader_60", "Digunakan:", v1);
            popup_row_follow(cr, cx, y + 74, "check_circle", "Gratis:", v2);
            popup_row_follow(cr, cx, y + 99, "empty_dashboard", "Total:", v3);
            cx += ram_w + 12;
        }
        if (show_swap) {
            popup_header(cr, cx, y + 19.5, "swap_horiz", "Swap");
            popup_row_follow(cr, cx, y + 49.5, "clock_loader_60", "Digunakan:", sw1);
            popup_row_follow(cr, cx, y + 74, "check_circle", "Gratis:", sw2);
            popup_row_follow(cr, cx, y + 99, "empty_dashboard", "Total:", sw3);
            cx += swap_w + 12;
        }
        if (show_cpu) {
            popup_header(cr, cx, y + 19.5, "planner_review", "CPU");
            popup_row_follow(cr, cx, y + 49.5, "bolt", "Beban:", cpu_s);
        }
        return;
    }

    if (s->popup == 1) { // clock
        double header_w = 17 + 5 + text_width(cr, s->clock_long_date, 15.7, VAR_HEAD);
        double uptime_w = 17 + 4 + text_width(cr, "Waktu aktif sistem:", 15, VAR_MAIN) +
                          4 + text_width(cr, s->uptime_str, 15, VAR_MAIN) + 6;
        double todo_label_w = 17 + 4 + text_width(cr, "Tugas:", 15, VAR_MAIN);
        double todo_w = text_width_max_line(cr, s->todo_text, 15, VAR_MAIN);
        double content_w = fmax(header_w, fmax(uptime_w, fmax(todo_label_w, todo_w)));
        double w = content_w + 20;
        double h = 110 + (s->todo_lines - 1) * 19;
        if (h < 110)
            h = 110;
        struct bar_layout l = bar_layout_for(s);
        double x = l.gx + 806.5 - w / 2.0;
        double y = above ? -50.0 - h : 50.0;
        draw_popup_card(cr, x, y, w, h);
        double cx = x + 10, right = x + w - 10;
        popup_header(cr, cx, y + 19.5, "calendar_month", s->clock_long_date);
        popup_row(cr, cx, right, y + 45.5, "timelapse",
                  "Waktu aktif sistem:", s->uptime_str);
        popup_row_follow(cr, cx, y + 71, "checklist", "Tugas:", NULL);
        draw_text_top_left(cr, s->todo_text, 15, VAR_MAIN,
                           cx, y + 80, C_ON_LAYER1);
        return;
    }

    // Battery
    int full = s->upower_valid ? (s->upower_state == 4) : s->battery_full;
    int charging = s->upower_valid ? (s->upower_state == 1) : s->battery_charging;
    long long tsec = charging
        ? (s->upower_valid ? s->upower_time_full : s->battery_minutes_left * 60LL)
        : (s->upower_valid ? s->upower_time_empty : s->battery_minutes_left * 60LL);
    double rate = s->upower_valid ? s->upower_rate : s->battery_power_w;
    double health = s->upower_valid ? s->upower_health : s->battery_health;
    int show_time = !(full || tsec <= 0 || rate <= 0.01);
    int show_rate = !(!full && rate == 0.0);

    char time_label[64] = "";
    char time_val[32] = "";
    if (show_time) {
        int mins = (int)(tsec / 60);
        if (mins >= 60)
            snprintf(time_val, sizeof time_val, "%dh, %dm", mins / 60, mins % 60);
        else
            snprintf(time_val, sizeof time_val, "%dm", mins);
        snprintf(time_label, sizeof time_label, "%s",
                 charging ? "Waktu hingga penuh:" : "Time to empty:");
    }
    char rate_label[64] = "";
    char rate_val[32] = "";
    if (show_rate) {
        snprintf(rate_label, sizeof rate_label, "%s",
                 full ? "Terisi penuh" : (charging ? "Mengisi:" : "Menguras:"));
        if (!full) {
            char num[32];
            fmt_dot2(num, sizeof num, rate);
            snprintf(rate_val, sizeof rate_val, "%sW", num);
        }
    }
    char health_val[32];
    char num[32];
    fmt_dot1(num, sizeof num, health);
    snprintf(health_val, sizeof health_val, "%s%%", num);

    double header_w = 17 + 5 + text_width(cr, "Baterai", 15.7, VAR_HEAD);
    double time_w = 0, rate_w = 0, health_w = 0;
    if (show_time)
        time_w = 17 + 4 + text_width(cr, time_label, 15, VAR_MAIN) +
                 4 + text_width(cr, time_val, 15, VAR_MAIN);
    if (show_rate) {
        rate_w = 17 + 4 + text_width(cr, rate_label, 15, VAR_MAIN);
        if (rate_val[0])
            rate_w += 4 + text_width(cr, rate_val, 15, VAR_MAIN);
    }
    health_w = 17 + 4 + text_width(cr, "Health:", 15, VAR_MAIN) +
               4 + text_width(cr, health_val, 15, VAR_MAIN);
    double content_w = fmax(header_w, health_w);
    if (show_time)
        content_w = fmax(content_w, time_w);
    if (show_rate)
        content_w = fmax(content_w, rate_w);
    double w = content_w + 16;
    double h = 116;
    if (!show_time) h -= 24.5;
    if (!show_rate) h -= 24.5;
    struct bar_layout l = bar_layout_for(s);
    double x = l.gx + 988.5 - w / 2.0;
    double y = above ? -50.0 - h : 50.0;
    draw_popup_card(cr, x, y, w, h);
    double cx = x + 10, right = x + w - 10;
    popup_header(cr, cx, y + 18.5, "battery_android_full", "Baterai");

    double row_y = y + 46;
    if (show_time) {
        popup_row(cr, cx, right, row_y, "schedule", time_label, time_val);
        row_y += 24.5;
    }
    if (show_rate) {
        popup_row(cr, cx, right, row_y, "bolt", rate_label, rate_val);
        row_y += 24;
    }
    popup_row(cr, cx, right, row_y, "heart_check", "Health:", health_val);
}

static const char *cache_home(void) {
    const char *cache = getenv("XDG_CACHE_HOME");
    if (cache && cache[0])
        return cache;
    static char path[512];
    if (!path[0]) {
        const char *home = getenv("HOME");
        if (!home)
            home = "/tmp";
        snprintf(path, sizeof path, "%s/.cache", home);
    }
    return path;
}

static const char *state_home(void) {
    const char *state = getenv("XDG_STATE_HOME");
    if (state && state[0])
        return state;
    static char path[512];
    if (!path[0]) {
        const char *home = getenv("HOME");
        if (!home)
            home = "/tmp";
        snprintf(path, sizeof path, "%s/.local/state", home);
    }
    return path;
}

static const char *notif_path(void) {
    static char path[512];
    if (!path[0])
        snprintf(path, sizeof path,
                 "%s/quickshell/notifications/notifications.json", cache_home());
    return path;
}

static time_t notif_mtime_now(void) {
    struct stat st;
    if (stat(notif_path(), &st) != 0)
        return 0;
    return st.st_mtime;
}

static const char *notif_state_path(void) {
    static char path[512];
    if (!path[0])
        snprintf(path, sizeof path,
                 "%s/quickshell/notifications/state.json", cache_home());
    return path;
}

static const char *colors_path(void) {
    static char path[640];
    if (!path[0])
        snprintf(path, sizeof path,
                 "%s/quickshell/user/generated/colors.json", state_home());
    return path;
}

static int parse_hex_color(const char *text, struct rgba *out) {
    if (!text || text[0] != '#')
        return 0;
    size_t n = strlen(text);
    if (n != 7 && n != 9)
        return 0;
    char buf[3] = {0};
    unsigned vals[4] = {0, 0, 0, 255};
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

static void refresh_theme(struct state *s) {
    const char *path = colors_path();
    struct stat st;
    if (stat(path, &st) < 0)
        return;
    if (s->theme_loaded && st.st_mtim.tv_sec == s->theme_mtime &&
        st.st_mtim.tv_nsec == s->theme_mtime_ns &&
        st.st_size == s->theme_size)
        return;

    struct json_object *root = json_object_from_file(path);
    if (!root)
        return;

    struct rgba base = C_LAYER0, primary = C_PRIMARY, on_primary = C_ON_PRIMARY;
    struct rgba on_bg = C_ON_LAYER0, on_variant = C_ON_LAYER1;
    struct rgba outline = C_SUBTEXT, error = C_ERROR;
    struct rgba sec = C_SECONDARY, on_sec = C_ON_SECONDARY;
    struct rgba popup = C_POPUP_BG;

    theme_color(root, "background", &base, 0.87);
    theme_color(root, "primary", &primary, 1.0);
    theme_color(root, "on_primary", &on_primary, 1.0);
    theme_color(root, "on_background", &on_bg, 1.0);
    theme_color(root, "on_surface_variant", &on_variant, 1.0);
    theme_color(root, "outline", &outline, 1.0);
    theme_color(root, "error", &error, 1.0);
    theme_color(root, "secondary_container", &sec, 1.0);
    theme_color(root, "on_secondary_container", &on_sec, 1.0);
    theme_color(root, "surface_container", &popup, 1.0);
    json_object_put(root);

    C_LAYER0 = base;
    C_LAYER0_SOLID = base;
    C_LAYER0_SOLID.a = 1.0;
    C_PRIMARY = primary;
    C_ON_PRIMARY = on_primary;
    C_ON_LAYER0 = on_bg;
    C_ON_LAYER1 = on_variant;
    C_SUBTEXT = outline;
    C_ERROR = error;
    C_SECONDARY = sec;
    C_ON_SECONDARY = on_sec;
    C_POPUP_BG = popup;
    C_OCCUPIED = sec;
    C_OCCUPIED.a = 0.6;
    C_DOT = outline;
    C_DOT.a = 0.505;
    C_SECONDARY_HV.r = 0.9 * sec.r + 0.1 * on_sec.r;
    C_SECONDARY_HV.g = 0.9 * sec.g + 0.1 * on_sec.g;
    C_SECONDARY_HV.b = 0.9 * sec.b + 0.1 * on_sec.b;
    C_SECONDARY_HV.a = 1.0;

    s->theme_mtime = st.st_mtim.tv_sec;
    s->theme_mtime_ns = st.st_mtim.tv_nsec;
    s->theme_size = st.st_size;
    s->theme_loaded = 1;
    s->redraw = 1;
}

static const char *todo_path(void) {
    static char path[512];
    if (!path[0])
        snprintf(path, sizeof path, "%s/quickshell/user/todo.json", state_home());
    return path;
}

static void read_notif_state(struct state *s) {
    FILE *f = fopen(notif_state_path(), "r");
    if (!f) {
        s->notif_state_valid = 0;
        return;
    }
    int unread = -1, silent = -1, show_count = 0;
    int n = fscanf(f, "%d %d %d", &unread, &silent, &show_count);
    fclose(f);
    if (n < 2)
        return; // A write may be in progress; keep the previous valid state.
    if (unread < 0)
        unread = 0;
    if (silent < 0)
        silent = 0;
    if (!s->notif_state_valid || unread != s->notif_unread ||
        silent != s->notif_silent || show_count != s->notif_show_count) {
        s->notif_state_valid = 1;
        s->notif_unread = unread;
        s->notif_silent = silent;
        s->notif_show_count = show_count;
        s->redraw = 1;
    }
}

/* ---- Minimal JSON helpers for Quickshell's todo.json ---- */

static const char *skip_json_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void utf8_append(char *out, size_t n, size_t *o, unsigned cp) {
    if (cp < 0x80) {
        if (*o + 1 < n) out[(*o)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*o + 2 < n) {
            out[(*o)++] = (char)(0xC0 | (cp >> 6));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (*o + 3 < n) {
            out[(*o)++] = (char)(0xE0 | (cp >> 12));
            out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (*o + 4 < n) {
            out[(*o)++] = (char)(0xF0 | (cp >> 18));
            out[(*o)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[(*o)++] = (char)(0x80 | (cp & 0x3F));
        }
    }
}

/* Parse a JSON string starting at p (which points to the opening quote). */
static int parse_json_string(const char *p, char *out, size_t n, const char **end) {
    if (*p != '"')
        return 0;
    p++;
    size_t o = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"': if (o + 1 < n) out[o++] = '"'; p++; break;
            case '\\': if (o + 1 < n) out[o++] = '\\'; p++; break;
            case '/': if (o + 1 < n) out[o++] = '/'; p++; break;
            case 'b': if (o + 1 < n) out[o++] = '\b'; p++; break;
            case 'f': if (o + 1 < n) out[o++] = '\f'; p++; break;
            case 'n': if (o + 1 < n) out[o++] = '\n'; p++; break;
            case 'r': if (o + 1 < n) out[o++] = '\r'; p++; break;
            case 't': if (o + 1 < n) out[o++] = '\t'; p++; break;
            case 'u': {
                unsigned cp = 0;
                int ok = 1;
                for (int i = 1; i <= 4; i++) {
                    int h = hex_digit((unsigned char)p[i]);
                    if (h < 0) { ok = 0; break; }
                    cp = (cp << 4) | (unsigned)h;
                }
                if (!ok) { p++; break; }
                p += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo = 0;
                    int ok2 = 1;
                    for (int i = 2; i <= 5; i++) {
                        int h = hex_digit((unsigned char)p[i]);
                        if (h < 0) { ok2 = 0; break; }
                        lo = (lo << 4) | (unsigned)h;
                    }
                    if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                utf8_append(out, n, &o, cp);
                break;
            }
            default:
                if (o + 1 < n) out[o++] = *p;
                p++;
                break;
            }
        } else {
            if (o + 1 < n) out[o++] = *p;
            p++;
        }
    }
    out[o] = 0;
    if (*p == '"')
        p++;
    if (end)
        *end = p;
    return 1;
}

static int find_key_string(const char *obj, const char *key, char *out, size_t n) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p)
        return 0;
    p += strlen(pat);
    p = skip_json_ws(p);
    if (*p != ':')
        return 0;
    p = skip_json_ws(p + 1);
    return parse_json_string(p, out, n, NULL);
}

static int find_key_bool(const char *obj, const char *key, int *out) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(obj, pat);
    if (!p)
        return 0;
    p += strlen(pat);
    p = skip_json_ws(p);
    if (*p != ':')
        return 0;
    p = skip_json_ws(p + 1);
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 1; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 1; }
    return 0;
}

static const char *matching_brace(const char *p) {
    int depth = 0;
    int in_string = 0;
    int escape = 0;
    for (; *p; p++) {
        if (in_string) {
            if (escape) escape = 0;
            else if (*p == '\\') escape = 1;
            else if (*p == '"') in_string = 0;
            continue;
        }
        if (*p == '"') in_string = 1;
        else if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0)
                return p;
        }
    }
    return NULL;
}

static void read_config(struct state *s) {
    if (!s->cfg_loaded) {
        s->cfg_show_memory = 1;
        s->cfg_show_cpu = 1;
        s->cfg_show_swap = 1;
        s->cfg_always_cpu = 1;
        s->cfg_always_swap = 1;
        s->cfg_mem_warn = 95;
        s->cfg_cpu_warn = 90;
        s->cfg_swap_warn = 85;
        s->cfg_battery_low = 20;
        s->cfg_auto_hide = 0;
        s->cfg_autohide_hover = 2;
        s->cfg_autohide_push = 0;
        s->cfg_bar_bottom = 0;
        s->cfg_loaded = 1;
    }

    dots_config_reload_if_changed(&s->cfg);

    int old_mem = s->cfg_show_memory, old_cpu = s->cfg_show_cpu;
    int old_swap = s->cfg_show_swap, old_acpu = s->cfg_always_cpu;
    int old_aswap = s->cfg_always_swap;
    int old_mw = s->cfg_mem_warn, old_cw = s->cfg_cpu_warn;
    int old_sw = s->cfg_swap_warn, old_blow = s->cfg_battery_low;
    int old_hide = s->cfg_auto_hide;

    s->cfg_show_memory = dots_config_bool(&s->cfg, "bar.resources.showMemory", 1);
    s->cfg_show_cpu = dots_config_bool(&s->cfg, "bar.resources.showCpu", 1);
    s->cfg_show_swap = dots_config_bool(&s->cfg, "bar.resources.showSwap", 1);
    s->cfg_always_cpu = dots_config_bool(&s->cfg, "bar.resources.alwaysShowCpu", 1);
    s->cfg_always_swap = dots_config_bool(&s->cfg, "bar.resources.alwaysShowSwap", 1);
    s->cfg_mem_warn = dots_config_int(&s->cfg, "bar.resources.memoryWarningThreshold", 95);
    s->cfg_cpu_warn = dots_config_int(&s->cfg, "bar.resources.cpuWarningThreshold", 90);
    s->cfg_swap_warn = dots_config_int(&s->cfg, "bar.resources.swapWarningThreshold", 85);
    s->cfg_battery_low = dots_config_int(&s->cfg, "battery.low", 20);
    s->cfg_auto_hide = dots_config_bool(&s->cfg, "bar.autoHide.enable", 0);
    s->cfg_autohide_hover = dots_config_int(&s->cfg, "bar.autoHide.hoverRegionWidth", 2);
    s->cfg_autohide_push = dots_config_bool(&s->cfg, "bar.autoHide.pushWindows", 0);
    s->cfg_bar_bottom = dots_config_bool(&s->cfg, "bar.bottom", 0);
    if (s->cfg_autohide_hover < 1)
        s->cfg_autohide_hover = 1;
    if (s->cfg_autohide_hover > BAR_H)
        s->cfg_autohide_hover = BAR_H;
    if (old_mem != s->cfg_show_memory || old_cpu != s->cfg_show_cpu ||
        old_swap != s->cfg_show_swap || old_acpu != s->cfg_always_cpu ||
        old_aswap != s->cfg_always_swap || old_mw != s->cfg_mem_warn ||
        old_cw != s->cfg_cpu_warn || old_sw != s->cfg_swap_warn ||
        old_blow != s->cfg_battery_low || old_hide != s->cfg_auto_hide)
        s->redraw = 1;

    // Enabling an indicator starts its data source immediately instead of
    // waiting for the next 10-second refresh tick.
    if ((!old_mem && s->cfg_show_memory) || (!old_swap && s->cfg_show_swap))
        read_mem(s);
    if (!old_cpu && s->cfg_show_cpu)
        read_cpu(s);
}

static void set_redirect(struct state *s, int *changed,
                         const char *text, int lines, int unfinished,
                         time_t mtime, off_t size) {
    if (strcmp(s->todo_text, text) != 0 || s->todo_lines != lines ||
        s->todo_unfinished != unfinished || s->todo_mtime != mtime ||
        s->todo_size != size) {
        snprintf(s->todo_text, sizeof s->todo_text, "%s", text);
        s->todo_lines = lines;
        s->todo_unfinished = unfinished;
        s->todo_mtime = mtime;
        s->todo_size = size;
        *changed = 1;
    }
}

static void load_todos(struct state *s) {
    struct stat st;
    if (stat(todo_path(), &st) == 0 && st.st_mtime == s->todo_mtime &&
        st.st_size == s->todo_size && s->todo_text[0])
        return;

    int changed = 0;
    if (stat(todo_path(), &st) != 0) {
        set_redirect(s, &changed, "Tidak ada tugas tertunda", 1, 0, 0, 0);
        if (changed)
            s->redraw = 1;
        return;
    }

    FILE *f = fopen(todo_path(), "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0)
        len = 0;
    if (len > (1 << 20))
        len = 1 << 20;
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = 0;

    char first[5][512];
    int nfirst = 0;
    int unfinished = 0;
    const char *p = buf;
    while ((p = strchr(p, '{')) != NULL) {
        const char *end = matching_brace(p);
        if (!end)
            break;
        size_t olen = (size_t)(end - p + 1);
        char *obj = malloc(olen + 1);
        if (!obj)
            break;
        memcpy(obj, p, olen);
        obj[olen] = 0;
        char content[512];
        int done = 0;
        if (find_key_string(obj, "content", content, sizeof content) && content[0]) {
            if (!find_key_bool(obj, "done", &done))
                done = 0;
            if (!done) {
                if (nfirst < 5)
                    snprintf(first[nfirst++], sizeof first[0], "%s", content);
                unfinished++;
            }
        }
        free(obj);
        p = end + 1;
    }
    free(buf);

    char text[4096];
    text[0] = 0;
    size_t off = 0;
    if (unfinished == 0) {
        snprintf(text, sizeof text, "Tidak ada tugas tertunda");
    } else {
        for (int i = 0; i < nfirst; i++) {
            int n = snprintf(text + off, sizeof text - off, "%s  %d. %s",
                             i ? "\n" : "", i + 1, first[i]);
            if (n > 0) off += (size_t)n;
            if (off >= sizeof text) off = sizeof text - 1;
        }
        if (unfinished > 5) {
            int n = snprintf(text + off, sizeof text - off,
                             "\n  ... dan %d lainnya", unfinished - 5);
            if (n > 0) off += (size_t)n;
            if (off >= sizeof text) off = sizeof text - 1;
        }
    }
    int lines = 1;
    for (const char *q = text; *q; q++)
        if (*q == '\n')
            lines++;
    set_redirect(s, &changed, text, lines, unfinished,
                 st.st_mtime, st.st_size);
    if (changed)
        s->redraw = 1;
}

static void draw(struct state *s) {
    if (!s->configured || !s->data || !s->buffer)
        return;

    struct bar_layout L = bar_layout_for(s);
    const double W = L.w;
    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        s->data, CAIRO_FORMAT_ARGB32, s->buf_w, s->buf_h, s->buf_w * 4);
    cairo_t *cr = cairo_create(cs);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (s->cfg_auto_hide && !s->bar_visible) {
        cairo_destroy(cr);
        cairo_surface_flush(cs);
        wl_surface_attach(s->surface, s->buffer, 0, 0);
        wl_surface_damage_buffer(s->surface, 0, 0, s->buf_w, s->buf_h);
        wl_surface_commit(s->surface);
        cairo_surface_destroy(cs);
        return;
    }

    if (s->bar_y > 0)
        cairo_translate(cr, 0, s->bar_y);

    // Bar background (cornerStyle 0: full width, square).
    cairo_set_source_rgba(cr, C_LAYER0.r, C_LAYER0.g, C_LAYER0.b, C_LAYER0.a);
    cairo_rectangle(cr, 0, 0, W, BAR_H);
    cairo_fill(cr);

    const double lg_group = L.lg_group;            // left group
    const double wg_group = L.wg_group;            // workspaces group
    const double rg_group = L.rg_group;            // clock group
    const double lg = L.lg;                        // content origin
    const double wg = L.wg;
    const double rg = L.rg;

    // Group backgrounds.
    cairo_set_source_rgba(cr, C_LAYER1.r, C_LAYER1.g, C_LAYER1.b, C_LAYER1.a);
    rounded_rect(cr, lg_group, 4, 360, 32, 12);
    cairo_fill(cr);
    rounded_rect(cr, wg_group, 4, 270, 32, 12);
    cairo_fill(cr);
    rounded_rect(cr, rg_group, 4, 360, 32, 12);
    cairo_fill(cr);

    // Left sidebar button background (behind the spark icon).
    // The real bar shows it whenever the pointer is anywhere in the left
    // section, not only directly over the button.
    {
        int hovered = (s->hover_mask & 1) != 0;
        if (s->lb_open || hovered) {
            struct rgba bg = s->lb_open
                ? (hovered ? C_SECONDARY_HV : C_SECONDARY)
                : C_LAYER1_HOVER;
            cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
            rounded_rect(cr, 23.0, 5.25, 29.5, 29.5, 14.75);
            cairo_fill(cr);
        }
    }

    // Left: spark icon.
    if (s->spark) {
        double sz = 19.5;
        cairo_save(cr);
        cairo_translate(cr, 37.75 - sz / 2.0, 20 - sz / 2.0);
        double sc = sz / cairo_image_surface_get_width(s->spark);
        cairo_scale(cr, sc, sc);
        cairo_set_source_rgba(cr, C_ON_LAYER0.r, C_ON_LAYER0.g, C_ON_LAYER0.b, 1);
        cairo_mask_surface(cr, s->spark, 0, 0);
        cairo_restore(cr);
    }

    // Left: active window.
    draw_text(cr, s->aw_class[0] ? s->aw_class : "Desktop", 12, VAR_MAIN,
              103.5, 63.0, 6, C_SUBTEXT);
    draw_text(cr, s->aw_title[0] ? s->aw_title : "", 15, VAR_MAIN,
              98.5, 63.5, 17.5, C_ON_LAYER0);

    // Resources + media. Resource visibility follows Quickshell's
    // Config.options.bar.resources: RAM, optional Swap, optional CPU. The
    // resource rows use fixed-width "100" text, so each badge is item_w wide.
    double w100 = 26.0; // Qt text metric for "100" at 15px, numbers family
    double res_x = lg + 4; // group padding 0 + row left margin
    double item_w = 20 + 2 + w100;
    double rx = res_x;
    double resources_w = 8; // row left + right margins
    char num[16];

    if (s->cfg_show_memory) {
        draw_badge(cr, rx + 10, 20, s->mem_pct, "memory",
                   s->mem_pct * 100 >= s->cfg_mem_warn);
        snprintf(num, sizeof num, "%d", (int)lround(s->mem_pct * 100));
        draw_text_centered(cr, num, 15, VAR_NUM,
                           rx + 20 + 2 + w100 / 2.0, 20, C_ON_LAYER1);
        rx += item_w;
        resources_w += item_w;
    }

    int show_swap = s->cfg_show_swap &&
        ((s->cfg_always_swap && s->swap_pct > 0) ||
         s->media_title[0] == 0);
    if (show_swap) {
        // Swap and CPU each carry Layout.leftMargin: shown ? 6 : 0.
        rx += 6;
        resources_w += 6;
        draw_badge(cr, rx + 10, 20, s->swap_pct, "swap_horiz",
                   s->swap_pct * 100 >= s->cfg_swap_warn);
        snprintf(num, sizeof num, "%d", (int)lround(s->swap_pct * 100));
        draw_text_centered(cr, num, 15, VAR_NUM,
                           rx + 20 + 2 + w100 / 2.0, 20, C_ON_LAYER1);
        rx += item_w;
        resources_w += item_w;
    }

    int show_cpu = s->cfg_show_cpu &&
        (s->cfg_always_cpu || s->media_title[0] == 0);
    if (show_cpu) {
        rx += 6;
        resources_w += 6;
        draw_badge(cr, rx + 10, 20, s->cpu_pct, "planner_review",
                   s->cpu_pct * 100 >= s->cfg_cpu_warn);
        snprintf(num, sizeof num, "%d", (int)lround(s->cpu_pct * 100));
        draw_text_centered(cr, num, 15, VAR_NUM,
                           rx + 20 + 2 + w100 / 2.0, 20, C_ON_LAYER1);
        rx += item_w;
        resources_w += item_w;
    }
    s->resources_w = resources_w;

    double media_x = lg + resources_w + 4;
    s->media_x = media_x;
    double progress = 0;
    if (s->media_len > 0)
        progress = s->media_pos / s->media_len;
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    int playing = strcmp(s->media_status, "Playing") == 0;
    draw_badge(cr, media_x + 10, 20, progress,
               playing ? "pause" : "music_note", 0);

    char media[1024];
    if (s->media_title[0])
        snprintf(media, sizeof media, "%s%s%s", s->media_title,
                 s->media_artist[0] ? " • " : "",
                 s->media_artist);
    else
        snprintf(media, sizeof media, "No media");
    double media_max = (lg + 350) - (media_x + 24) - 4;
    if (media_max < 0) media_max = 0;
    draw_text_mid(cr, media, 15, VAR_MAIN, media_max, media_x + 24, 20, C_ON_LAYER1);

    // Workspaces.
    for (int i = 0; i < NWS; i++) {
        if (!s->occupied[i])
            continue;
        int prev = i > 0 && s->occupied[i - 1];
        int next = i < NWS - 1 && s->occupied[i + 1];
        double pw = WS_W * (1 + 0.5 * prev + 0.5 * next);
        double px = wg + i * WS_W - 13 * prev;
        rounded_rect(cr, px, 7, pw, 26, 13);
        cairo_set_source_rgba(cr, C_OCCUPIED.r, C_OCCUPIED.g, C_OCCUPIED.b, C_OCCUPIED.a);
        cairo_fill(cr);
    }

    int active_idx = -1;
    if (s->active_ws >= 1 && s->active_ws <= NWS)
        active_idx = s->active_ws - 1;
    if (active_idx >= 0) {
        cairo_set_source_rgba(cr, C_PRIMARY.r, C_PRIMARY.g, C_PRIMARY.b, C_PRIMARY.a);
        rounded_rect(cr, wg + active_idx * WS_W + 2, 9, 22, 22, 11);
        cairo_fill(cr);
    }

    for (int i = 0; i < NWS; i++) {
        double cx = wg + i * WS_W + 13;
        if (s->occupied[i]) {
            if (s->ws_icon[i]) {
                draw_app_icon(cr, s->ws_icon[i],
                              wg + i * WS_W + 4, 11, 18);
            } else {
                cairo_set_source_rgba(cr, C_ON_SECONDARY.r, C_ON_SECONDARY.g,
                                      C_ON_SECONDARY.b, 1);
                cairo_arc(cr, cx, 20, 2.34, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        } else {
            cairo_set_source_rgba(cr, C_DOT.r, C_DOT.g, C_DOT.b, C_DOT.a);
            cairo_arc(cr, cx, 20, 2.34, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    // Clock.
    PangoLayout *lt = make_layout(cr, s->clock_time, 17, VAR_MAIN, 0);
    PangoLayout *ld = make_layout(cr, "•", 15, VAR_MAIN, 0);
    PangoLayout *lD = make_layout(cr, s->clock_date, 15, VAR_MAIN, 0);
    int wt, ht, wd, hd, wD, hD;
    pango_layout_get_pixel_size(lt, &wt, &ht);
    pango_layout_get_pixel_size(ld, &wd, &hd);
    pango_layout_get_pixel_size(lD, &wD, &hD);
    double total = wt + 4 + wd + 4 + wD;
    double clock_cx = L.gx + 794.0;
    double cx0 = clock_cx - total / 2.0;
    cairo_set_source_rgba(cr, C_ON_LAYER1.r, C_ON_LAYER1.g, C_ON_LAYER1.b, 1);
    cairo_move_to(cr, cx0, 19 - ht / 2.0);
    pango_cairo_show_layout(cr, lt);
    cx0 += wt + 4;
    cairo_move_to(cr, cx0, 20 - hd / 2.0);
    pango_cairo_show_layout(cr, ld);
    cx0 += wd + 4;
    cairo_move_to(cr, cx0, 20 - hD / 2.0);
    pango_cairo_show_layout(cr, lD);
    g_object_unref(lt);
    g_object_unref(ld);
    g_object_unref(lD);

    // Battery.
    double bat_x = rg + 350 - 30;
    draw_battery(cr, bat_x, 11, s->battery_pct / 100.0, s->battery_charging,
                 s->battery_pct <= s->cfg_battery_low);

    // Right indicators: bell (+ unread dot), wifi, bluetooth.
    // With the notification indicator shown the button is 107px wide
    // (20 padding + 3*19 icons + 2*15 spacing); without it 73px.
    double btn_right = W - 23;
    double btn_w = s->bell_visible ? 107 : 73;
    double icons_start = btn_right - btn_w + 10;
    double first_cx = icons_start + 9.5;
    double bell_cx = first_cx;
    double wifi_cx = s->bell_visible ? first_cx + 34 : first_cx;
    double bt_cx = wifi_cx + 34;

    int rhover = (s->hover_mask & 2) != 0;
    if (s->rb_open || rhover) {
        struct rgba bg = s->rb_open
            ? (rhover ? C_SECONDARY_HV : C_SECONDARY)
            : C_LAYER1_HOVER;
        cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, bg.a);
        rounded_rect(cr, btn_right - btn_w, 3.5, btn_w, 33, 16.5);
        cairo_fill(cr);
    }

    struct rgba icon_col = s->rb_open ? C_ON_SECONDARY : C_ON_LAYER0;
    if (s->bell_visible) {
        draw_icon_mid(cr, s->notif_silent ? "notifications_paused" : "notifications",
                      19, 0, bell_cx, 20, icon_col);
        if (!s->notif_silent && s->notif_unread > 0) {
            if (s->notif_show_count) {
                char count[16];
                snprintf(count, sizeof count, "%d", s->notif_unread);
                PangoLayout *cl = make_layout(cr, count, 10, VAR_MAIN, 0);
                int tw, th;
                pango_layout_get_pixel_size(cl, &tw, &th);
                g_object_unref(cl);
                double d = fmax(tw, th);
                // Anchored to the top-right of the 19px icon item, matching
                // NotificationUnreadCount.qml (top/right margins 0).
                double bx = bell_cx + 9.5 - d;
                double by = 8.5;
                cairo_set_source_rgba(cr, C_ON_LAYER0.r, C_ON_LAYER0.g,
                                      C_ON_LAYER0.b, C_ON_LAYER0.a);
                cairo_arc(cr, bx + d / 2.0, by + d / 2.0, d / 2.0, 0, 2 * M_PI);
                cairo_fill(cr);
                draw_text_centered(cr, count, 10, VAR_MAIN,
                                   bx + d / 2.0, by + d / 2.0, C_LAYER0_SOLID);
            } else {
                cairo_set_source_rgba(cr, C_ON_LAYER0.r, C_ON_LAYER0.g,
                                      C_ON_LAYER0.b, C_ON_LAYER0.a);
                cairo_arc(cr, bell_cx + 4.5, 15.5, 4, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }

    draw_icon_mid(cr, "signal_wifi_4_bar", 19, 0, wifi_cx, 20, icon_col);
    draw_icon_mid(cr, "bluetooth", 19, 0, bt_cx, 20, icon_col);

    // Hover scroll hints at the screen edges (brightness / volume).
    if (s->hover_mask & 1) {
        draw_icon_mid(cr, "keyboard_arrow_up", 14, 0, 7, 7, C_SUBTEXT);
        draw_icon_mid(cr, "light_mode", 14, 0, 7, 19, C_SUBTEXT);
        draw_icon_mid(cr, "keyboard_arrow_down", 14, 0, 7, 31, C_SUBTEXT);
    }
    if (s->hover_mask & 2) {
        draw_icon_mid(cr, "keyboard_arrow_up", 14, 0, W - 7, 7, C_SUBTEXT);
        draw_icon_mid(cr, "volume_up", 14, 0, W - 7, 19, C_SUBTEXT);
        draw_icon_mid(cr, "keyboard_arrow_down", 14, 0, W - 7, 31, C_SUBTEXT);
    }

    draw_popups(s, cr, s->cfg_bar_bottom);

    cairo_destroy(cr);
    cairo_surface_flush(cs);

    wl_surface_attach(s->surface, s->buffer, 0, 0);
    wl_surface_damage_buffer(s->surface, 0, 0, s->buf_w, s->buf_h);
    wl_surface_commit(s->surface);

    cairo_surface_destroy(cs);
}

/* ---------------- Wayland ---------------- */

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
    struct state *s = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w == 0)
        w = s->out_w > 0 ? (uint32_t)s->out_w : 1366;
    if (h == 0)
        h = SURFACE_H;
    if ((int)w != s->buf_w || (int)h != s->buf_h)
        create_buffer(s, (int)w, (int)h);
    s->bar_y = s->cfg_bar_bottom ? (double)h - BAR_H : 0;

    // Only the visible bar strip is interactive; the shadow strip below
    // stays click-through.
    s->configured = 1;
    apply_layer_state(s);
    s->redraw = 1;
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    (void)data;
    (void)ls;
    exit(0);
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure,
    .closed = ls_closed,
};

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
                            int32_t pw, int32_t ph, int32_t subpixel,
                            const char *make, const char *model, int32_t transform) {
    (void)data; (void)o; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    (void)o; (void)flags; (void)refresh;
    struct state *s = data;
    s->out_w = width;
    s->out_h = height;
}

static void output_done(void *data, struct wl_output *o) { (void)data; (void)o; }
static void output_scale(void *data, struct wl_output *o, int32_t factor) {
    (void)data; (void)o; (void)factor;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

/* ---------------- Pointer input ---------------- */

static void ptr_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                      struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)serial;
    struct state *s = data;
    if (surface != s->surface)
        return;
    s->ptr_inside = 1;
    s->ptr_x = wl_fixed_to_double(sx);
    s->ptr_y = wl_fixed_to_double(sy);
    show_bar(s);
    update_hover(s);
}

static void ptr_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                      struct wl_surface *surface) {
    (void)pointer; (void)serial;
    struct state *s = data;
    if (surface != s->surface)
        return;
    s->ptr_inside = 0;
    s->last_pointer_ms = mono_ms();
    update_hover(s);
}

static void ptr_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                       wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    struct state *s = data;
    s->ptr_x = wl_fixed_to_double(sx);
    s->ptr_y = wl_fixed_to_double(sy);
    show_bar(s);
    update_hover(s);
}

static void ptr_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                       uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    struct state *s = data;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    s->ptr_inside = 1;
    s->last_pointer_ms = mono_ms();
    handle_click(s, button, s->ptr_x, s->ptr_y - s->bar_y);
}

static void ptr_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                     uint32_t axis, wl_fixed_t value) {
    (void)pointer; (void)time;
    struct state *s = data;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return;
    handle_scroll(s, s->ptr_x, s->ptr_y - s->bar_y, wl_fixed_to_double(value));
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct state *s = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
        s->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(s->pointer, &pointer_listener, s);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s->pointer) {
        wl_pointer_release(s->pointer);
        s->pointer = NULL;
        s->ptr_inside = 0;
        update_hover(s);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version) {
    struct state *s = data;
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        s->compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
                                         version < 4 ? version : 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        s->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        s->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface,
                                          version < 4 ? version : 4);
    } else if (strcmp(iface, wl_output_interface.name) == 0 && !s->output) {
        s->output = wl_registry_bind(reg, name, &wl_output_interface,
                                     version < 1 ? version : 1);
        wl_output_add_listener(s->output, &output_listener, s);
    } else if (strcmp(iface, wl_seat_interface.name) == 0 && !s->seat) {
        s->seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
        wl_seat_add_listener(s->seat, &seat_listener, s);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int main(void) {
    setlocale(LC_TIME, "");
    init_base();
    signal(SIGCHLD, SIG_IGN);

    struct state s;
    memset(&s, 0, sizeof s);
    dots_config_init(&s.cfg);
    dots_config_load(&s.cfg);
    s.bar_visible = 1;
    s.active_ws = 1;
    s.resources_w = 110; // current default: RAM + CPU, no Swap
    s.media_x = 298;
    const char *forced_popup = getenv("CPROTO_FORCE_POPUP");
    if (forced_popup)
        s.popup = atoi(forced_popup);
    s.hypr_buf = malloc(1024 * 512);

    read_config(&s);
    refresh_theme(&s);
    if (s.cfg_auto_hide)
        s.bar_visible = 0;
    s.bar_y = s.cfg_bar_bottom ? SURFACE_H - BAR_H : 0;

    s.icon_kitty = load_png("kitty.png");
    s.icon_brave = load_png("brave.png");
    s.icon_dolphin = load_png("dolphin.png");
    s.spark = load_png("spark_icon.png");

    refresh_clock(&s);
    refresh_hypr(&s);
    refresh_sidebars(&s);
    if (s.cfg_show_memory || s.cfg_show_swap)
        read_mem(&s);
    if (s.cfg_show_cpu)
        read_cpu(&s);
    read_battery(&s);
    refresh_media(&s);
    s.notif_mtime = notif_mtime_now();
    s.last_res = time(NULL);
    s.last_media = s.last_res;

    s.display = wl_display_connect(NULL);
    if (!s.display) {
        fprintf(stderr, "dots-bar: cannot connect to Wayland\n");
        return 1;
    }

    s.registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(s.registry, &registry_listener, &s);
    wl_display_roundtrip(s.display);

    if (!s.compositor || !s.shm || !s.layer_shell) {
        fprintf(stderr, "dots-bar: missing wayland globals\n");
        return 1;
    }

    s.surface = wl_compositor_create_surface(s.compositor);
    struct wl_region *empty = wl_compositor_create_region(s.compositor);
    wl_surface_set_input_region(s.surface, empty);
    wl_region_destroy(empty);

    // Overlay layer so the bar covers the real bar while testing. The
    // namespace is configurable so the prototype and the new native bar can
    // coexist during migration without sharing Hyprland layer rules.
    const char *namespace = getenv("DOTS_BAR_NAMESPACE");
    if (!namespace || !namespace[0])
        namespace = "dots:bar";
    s.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        s.layer_shell, s.surface, s.output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, namespace);

    zwlr_layer_surface_v1_add_listener(s.layer_surface, &ls_listener, &s);
    zwlr_layer_surface_v1_set_size(s.layer_surface, 0, SURFACE_H);
    zwlr_layer_surface_v1_set_anchor(
        s.layer_surface,
        (s.cfg_bar_bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                          : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(s.layer_surface, 40);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        s.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(s.surface);

    wl_display_roundtrip(s.display);

    time_t start = time(NULL);
    while (1) {
        wl_display_dispatch_pending(s.display);
        wl_display_flush(s.display);

        struct pollfd pfd = { .fd = wl_display_get_fd(s.display), .events = POLLIN };
        int pr = poll(&pfd, 1, 200);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            if (wl_display_dispatch(s.display) < 0)
                break;
        }

        time_t now = time(NULL);
        if (now != s.last_sec) {
            s.last_sec = now;
            refresh_clock(&s);
            refresh_hypr(&s);
            refresh_sidebars(&s);
            s.redraw = 1;
        }
        if (now - s.last_res >= 10) {
            // Match ResourceUsage.qml's lazy polling: don't read an indicator
            // that is currently disabled in the config.
            if (s.cfg_show_memory || s.cfg_show_swap)
                read_mem(&s);
            if (s.cfg_show_cpu)
                read_cpu(&s);
            read_battery(&s);
            s.last_res = now;
            s.redraw = 1;
        }
        if (now - s.last_res >= 2)
            refresh_theme(&s);
        if (now - s.last_media >= 5) {
            refresh_media(&s);
            s.last_media = now;
            s.redraw = 1;
        }
        // Second CPU sample shortly after startup so the value isn't 0.
        if (start && now - start == 1 && s.cpu_total) {
            read_cpu(&s);
            s.redraw = 1;
        }
        if (s.cfg_auto_hide && s.bar_visible && !s.ptr_inside &&
            !s.popup && !forced_popup &&
            mono_ms() - s.last_pointer_ms > 300)
            hide_bar(&s);

        if (s.redraw && s.configured) {
            s.redraw = 0;
            draw(&s);
        }
    }

    wl_display_disconnect(s.display);
    dots_config_destroy(&s.cfg);
    return 0;
}
