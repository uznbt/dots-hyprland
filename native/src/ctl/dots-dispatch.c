#define _GNU_SOURCE
#include "common/ipc.h"
#include "common/log.h"
#include "common/paths.h"

#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(FILE *out) {
    fprintf(out,
        "usage: dots-dispatch <target> <action>\n"
        "\n"
        "Contoh:\n"
        "  dots-dispatch sidebarRight toggle\n"
        "  dots-dispatch mpris playPause\n"
        "\n"
        "Selama transisi, aksi didelegasikan ke Quickshell. Kalau dotsd tidak\n"
        "hidup, dispatcher memanggil qs langsung.\n");
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

    if (dispatch_daemon(target, action) == 0)
        return 0;

    DOTS_DEBUG("dotsd tidak tersedia; fallback ke qs");
    return fallback_qs(target, action);
}
