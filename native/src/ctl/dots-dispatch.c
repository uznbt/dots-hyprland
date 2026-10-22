#define _GNU_SOURCE
#include "common/ipc.h"
#include "common/log.h"
#include "common/paths.h"

#include <errno.h>
#include <json-c/json.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static void usage(FILE *out) {
    fprintf(out,
        "usage: dots-dispatch <target> <action>\n"
        "\n"
        "Contoh:\n"
        "  dots-dispatch sidebarRight toggle\n"
        "  dots-dispatch mpris playPause\n"
        "  dots-dispatch volume up|down|mute\n"
        "  dots-dispatch brightness up|down\n"
        "\n"
        "Volume/brightness memakai dots-osd bila tersedia. Aksi panel lain\n"
        "didelegasikan ke Quickshell selama transisi. Kalau dotsd tidak hidup,\n"
        "dispatcher memanggil qs langsung.\n");
}

static int find_sibling(const char *name, char *out, size_t size) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = 0;
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = 0;
            n = snprintf(out, size, "%s/%s", self, name);
            if (n > 0 && (size_t)n < size && access(out, X_OK) == 0)
                return 0;
        }
    }
    const char *path = getenv("PATH");
    if (!path)
        return -1;
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", path);
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir;
         dir = strtok_r(NULL, ":", &save)) {
        n = snprintf(out, size, "%s/%s", dir, name);
        if (n > 0 && (size_t)n < size && access(out, X_OK) == 0)
            return 0;
    }
    return -1;
}

static int fallback_qs(const char *target, const char *action);

static int name_is_internal(const char *name) {
    return strncasecmp(name, "eDP", 3) == 0 ||
           strncasecmp(name, "LVDS", 4) == 0 ||
           strncasecmp(name, "DSI", 3) == 0;
}

// Brightness native saat ini memakai brightnessctl (backlight internal).
// Monitor eksternal tetap lewat Quickshell/ddcutil sampai backend DDC native
// tersedia, supaya keybind tidak menurunkan brightness panel yang salah.
static int focused_output_is_internal(void) {
    const char *forced = getenv("DOTS_OUTPUT");
    if (forced && forced[0])
        return name_is_internal(forced);

    FILE *fp = popen("hyprctl monitors -j 2>/dev/null", "r");
    if (!fp)
        return 0;
    char json[64 * 1024];
    size_t off = 0;
    while (off < sizeof json - 1) {
        size_t n = fread(json + off, 1, sizeof json - 1 - off, fp);
        if (n == 0)
            break;
        off += n;
    }
    json[off] = 0;
    pclose(fp);

    int internal = 0;
    json_object *root = json_tokener_parse(json);
    if (root && json_object_is_type(root, json_type_array)) {
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
                internal = name_is_internal(json_object_get_string(name));
                break;
            }
        }
    }
    if (root)
        json_object_put(root);
    return internal;
}

static int spawn_process(char *const argv[]) {
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    return rc == 0 ? 0 : -1;
}

static int spawn_osd(const char *target, const char *action) {
    char exe[PATH_MAX];
    if (find_sibling("dots-osd", exe, sizeof exe) < 0)
        return -1;

    if (strcmp(target, "volume") == 0) {
        if (strcmp(action, "up") != 0 && strcmp(action, "down") != 0 &&
            strcmp(action, "mute") != 0)
            return -1;
    } else if (strcmp(target, "brightness") == 0) {
        if (strcmp(action, "up") != 0 && strcmp(action, "down") != 0 &&
            strcmp(action, "increment") != 0 &&
            strcmp(action, "decrement") != 0)
            return -1;
        if (!focused_output_is_internal())
            return -1;
    } else {
        return -1;
    }

    const char *osd_action = action;
    if (strcmp(action, "increment") == 0)
        osd_action = "up";
    else if (strcmp(action, "decrement") == 0)
        osd_action = "down";

    char *argv[] = { exe, (char *)target, (char *)osd_action, NULL };
    pid_t pid;
    int rc = posix_spawn(&pid, exe, NULL, NULL, argv, environ);
    return rc == 0 ? 0 : -1;
}

static int fallback_volume(const char *action) {
    if (strcmp(action, "up") == 0) {
        char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                         "2%+", "-l", "1.5", NULL };
        return spawn_process(argv);
    }
    if (strcmp(action, "down") == 0) {
        char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@",
                         "2%-", NULL };
        return spawn_process(argv);
    }
    if (strcmp(action, "mute") == 0) {
        char *argv[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@",
                         "toggle", NULL };
        return spawn_process(argv);
    }
    return -1;
}

static int fallback_brightness(const char *action) {
    const char *qs_action = strcmp(action, "down") == 0 ||
                            strcmp(action, "decrement") == 0
                                ? "decrement" : "increment";
    if (fallback_qs("brightness", qs_action) == 0)
        return 0;

    const char *delta = strcmp(action, "down") == 0 ||
                        strcmp(action, "decrement") == 0
                            ? "5%-" : "5%+";
    char *argv[] = { "brightnessctl", "s", (char *)delta, "--quiet", NULL };
    return spawn_process(argv);
}

static const char *qs_config(void) {
    const char *config = getenv("DOTS_QS_CONFIG");
    return (config && config[0]) ? config : "ii";
}

static int fallback_qs(const char *target, const char *action) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "dots-dispatch: fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        execlp("qs", "qs", "-c", qs_config(), "ipc", "call",
               target, action, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int dispatch_daemon(const char *target, const char *action) {
    char sock[1024];
    if (dots_socket_path(sock, sizeof sock) < 0)
        return -1;
    int fd = dots_ipc_client_connect(sock);
    if (fd < 0)
        return -1;

    json_object *req = json_object_new_object();
    if (!req) {
        close(fd);
        return -1;
    }
    json_object_object_add(req, "id", json_object_new_int(1));
    json_object_object_add(req, "cmd", json_object_new_string("panel"));
    json_object_object_add(req, "target", json_object_new_string(target));
    json_object_object_add(req, "action", json_object_new_string(action));

    const char *line = json_object_to_json_string_ext(
        req, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    int ok = -1;
    if (dots_ipc_send_line(fd, line) == 0) {
        char response[DOTS_IPC_MAX];
        if (dots_ipc_recv_line(fd, response, sizeof response) > 0 &&
            strstr(response, "\"ok\":true"))
            ok = 0;
    }
    json_object_put(req);
    close(fd);
    return ok;
}

int main(int argc, char **argv) {
    dots_log_init_from_env();
    if (argc < 3) {
        usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }

    const char *target = argv[1];
    const char *action = argv[2];
    if (!target[0] || !action[0]) {
        usage(stderr);
        return 2;
    }

    // OSD native mengambil alih volume/brightness. Kalau binary belum ada,
    // jalur lama tetap dipakai supaya keybind tidak pernah mati.
    if (strcmp(target, "volume") == 0) {
        if (spawn_osd(target, action) == 0)
            return 0;
        if (fallback_volume(action) == 0)
            return 0;
        return 1;
    }
    if (strcmp(target, "brightness") == 0) {
        if (spawn_osd(target, action) == 0)
            return 0;
        if (fallback_brightness(action) == 0)
            return 0;
        return 1;
    }

    if (dispatch_daemon(target, action) == 0)
        return 0;

    DOTS_DEBUG("dotsd tidak tersedia; fallback ke qs");
    return fallback_qs(target, action);
}
