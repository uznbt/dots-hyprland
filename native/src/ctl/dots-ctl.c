#define _GNU_SOURCE
#include "common/ipc.h"
#include "common/log.h"
#include "common/paths.h"

#include <errno.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *out) {
    fprintf(out,
        "usage: dots-ctl <command> [args]\n"
        "\n"
        "commands:\n"
        "  ping\n"
        "  status\n"
        "  config-get <dotted.path>\n"
        "  config-reload\n"
        "  shutdown\n"
        "  raw <json>\n"
        "\n"
        "Panels belum dipindahkan; command toggle/open/close akan ditolak\n"
        "sampai dotsd memiliki pemilik panel native.\n");
}

static int send_request(json_object *req) {
    char sock[1024];
    if (dots_socket_path(sock, sizeof sock) < 0) {
        fprintf(stderr, "dots-ctl: path socket terlalu panjang\n");
        return 1;
    }
    int fd = dots_ipc_client_connect(sock);
    if (fd < 0) {
        fprintf(stderr, "dots-ctl: tidak bisa connect ke %s: %s\n",
                sock, strerror(errno));
        return 1;
    }

    const char *line = json_object_to_json_string_ext(
        req, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    if (dots_ipc_send_line(fd, line) < 0) {
        fprintf(stderr, "dots-ctl: gagal mengirim request\n");
        close(fd);
        return 1;
    }

    char response[DOTS_IPC_MAX];
    ssize_t n = dots_ipc_recv_line(fd, response, sizeof response);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "dots-ctl: tidak ada response\n");
        return 1;
    }
    puts(response);

    json_object *resp = json_tokener_parse(response);
    int ok = 0;
    if (resp) {
        json_object *v = NULL;
        if (json_object_object_get_ex(resp, "ok", &v) &&
            json_object_is_type(v, json_type_boolean))
            ok = json_object_get_boolean(v);
        json_object_put(resp);
    }
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    dots_log_init_from_env();
    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    const char *cmd = argv[1];
    const char *wire_cmd = cmd;
    if (strcmp(cmd, "config-get") == 0)
        wire_cmd = "config.get";
    else if (strcmp(cmd, "config-reload") == 0)
        wire_cmd = "config.reload";

    json_object *req = json_object_new_object();
    if (!req)
        return 1;
    json_object_object_add(req, "id", json_object_new_int(1));
    json_object_object_add(req, "cmd", json_object_new_string(wire_cmd));

    if (strcmp(cmd, "config-get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "dots-ctl: config-get butuh path\n");
            json_object_put(req);
            return 2;
        }
        json_object_object_add(req, "path", json_object_new_string(argv[2]));
    } else if (strcmp(cmd, "raw") == 0) {
        if (argc < 3) {
            fprintf(stderr, "dots-ctl: raw butuh JSON\n");
            json_object_put(req);
            return 2;
        }
        json_object_put(req);
        req = json_tokener_parse(argv[2]);
        if (!req || !json_object_is_type(req, json_type_object)) {
            fprintf(stderr, "dots-ctl: raw bukan JSON object\n");
            if (req)
                json_object_put(req);
            return 2;
        }
    } else if (strcmp(cmd, "ping") != 0 &&
               strcmp(cmd, "status") != 0 &&
               strcmp(cmd, "config-reload") != 0 &&
               strcmp(cmd, "shutdown") != 0) {
        /* Teruskan command generik; dotsd yang memutuskan valid/tidaknya. */
        if (argc >= 3)
            json_object_object_add(req, "target", json_object_new_string(argv[2]));
    }

    int rc = send_request(req);
    json_object_put(req);
    return rc;
}
