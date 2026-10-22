#define _GNU_SOURCE
#include "common/config.h"
#include "common/ipc.h"
#include "common/log.h"
#include "common/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define DOTS_VERSION "0.1.0"
#define MAX_CLIENTS 32

struct client {
    int fd;
    char buf[DOTS_IPC_MAX];
    size_t used;
};

static volatile sig_atomic_t running = 1;
static int listen_fd = -1;
static char socket_path[1024];
static char pid_path[1024];

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static int write_pid_file(void) {
    FILE *f = fopen(pid_path, "w");
    if (!f)
        return -1;
    fprintf(f, "%ld\n", (long)getpid());
    fclose(f);
    return 0;
}

static void cleanup(void) {
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
    if (socket_path[0])
        unlink(socket_path);
    if (pid_path[0])
        unlink(pid_path);
}

static int setup_socket(void) {
    char runtime[1024];
    if (dots_runtime_dir(runtime, sizeof runtime) < 0 ||
        dots_socket_path(socket_path, sizeof socket_path) < 0 ||
        dots_pid_path(pid_path, sizeof pid_path) < 0) {
        DOTS_ERROR("runtime path terlalu panjang");
        return -1;
    }
    if (dots_mkdir_p(runtime, 0700) < 0) {
        DOTS_ERROR("gagal membuat %s: %s", runtime, strerror(errno));
        return -1;
    }

    /* Jangan membunuh instance yang masih hidup. Socket basi boleh dibuang. */
    int probe = dots_ipc_client_connect(socket_path);
    if (probe >= 0) {
        close(probe);
        DOTS_ERROR("dotsd sudah berjalan (%s)", socket_path);
        return -1;
    }
    unlink(socket_path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        DOTS_ERROR("socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    int n = snprintf(addr.sun_path, sizeof addr.sun_path, "%s", socket_path);
    if (n < 0 || (size_t)n >= sizeof addr.sun_path) {
        DOTS_ERROR("socket path terlalu panjang");
        return -1;
    }
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        chmod(socket_path, 0600) < 0 ||
        listen(listen_fd, 16) < 0) {
        DOTS_ERROR("bind/listen: %s", strerror(errno));
        return -1;
    }
    if (write_pid_file() < 0)
        DOTS_WARN("tidak bisa menulis pid file: %s", strerror(errno));
    return 0;
}

static json_object *make_response(json_object *request, int ok, const char *error) {
    json_object *resp = json_object_new_object();
    if (!resp)
        return NULL;

    json_object *id = NULL;
    if (request && json_object_object_get_ex(request, "id", &id))
        json_object_object_add(resp, "id", json_object_get(id));
    json_object_object_add(resp, "ok", json_object_new_boolean(ok));
    if (error)
        json_object_object_add(resp, "error", json_object_new_string(error));
    return resp;
}

static void add_data_string(json_object *resp, const char *key, const char *value) {
    json_object *data = NULL;
    if (!json_object_object_get_ex(resp, "data", &data)) {
        data = json_object_new_object();
        json_object_object_add(resp, "data", data);
    }
    json_object_object_add(data, key, json_object_new_string(value));
}

static void add_data_int(json_object *resp, const char *key, long long value) {
    json_object *data = NULL;
    if (!json_object_object_get_ex(resp, "data", &data)) {
        data = json_object_new_object();
        json_object_object_add(resp, "data", data);
    }
    json_object_object_add(data, key, json_object_new_int64(value));
}

static int send_response(int fd, json_object *resp) {
    const char *line = json_object_to_json_string_ext(
        resp, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    int rc = dots_ipc_send_line(fd, line);
    json_object_put(resp);
    return rc;
}

static int spawn_qs_panel(const char *target, const char *action) {
    const char *config = getenv("DOTS_QS_CONFIG");
    if (!config || !config[0])
        config = "ii";

    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execlp("qs", "qs", "-c", config, "ipc", "call",
               target, action, (char *)NULL);
        _exit(127);
    }
    return 0;
}

static void handle_request(struct dots_config *cfg, time_t started, int fd,
                           const char *line) {
    json_object *req = json_tokener_parse(line);
    if (!req || !json_object_is_type(req, json_type_object)) {
        if (req)
            json_object_put(req);
        json_object *resp = make_response(NULL, 0, "invalid JSON object");
        send_response(fd, resp);
        return;
    }

    json_object *cmd_obj = NULL;
    const char *cmd = NULL;
    if (json_object_object_get_ex(req, "cmd", &cmd_obj) &&
        json_object_is_type(cmd_obj, json_type_string))
        cmd = json_object_get_string(cmd_obj);

    if (!cmd || !cmd[0]) {
        send_response(fd, make_response(req, 0, "missing cmd"));
        json_object_put(req);
        return;
    }

    if (strcmp(cmd, "ping") == 0) {
        json_object *resp = make_response(req, 1, NULL);
        add_data_string(resp, "name", "dotsd");
        add_data_string(resp, "version", DOTS_VERSION);
        send_response(fd, resp);
    } else if (strcmp(cmd, "status") == 0) {
        json_object *resp = make_response(req, 1, NULL);
        add_data_string(resp, "name", "dotsd");
        add_data_string(resp, "version", DOTS_VERSION);
        add_data_int(resp, "pid", (long long)getpid());
        add_data_int(resp, "uptime_sec", (long long)(time(NULL) - started));
        add_data_int(resp, "config_loaded", cfg->loaded ? 1 : 0);
        add_data_string(resp, "config_path", cfg->path);
        send_response(fd, resp);
    } else if (strcmp(cmd, "config.reload") == 0) {
        int rc = dots_config_load(cfg);
        json_object *resp = make_response(req, rc == 0,
                                           rc == 0 ? NULL : "config reload failed");
        if (rc == 0)
            add_data_int(resp, "loaded", 1);
        send_response(fd, resp);
    } else if (strcmp(cmd, "config.get") == 0) {
        json_object *path_obj = NULL;
        const char *path = NULL;
        if (json_object_object_get_ex(req, "path", &path_obj) &&
            json_object_is_type(path_obj, json_type_string))
            path = json_object_get_string(path_obj);
        if (!path || !path[0]) {
            send_response(fd, make_response(req, 0, "missing path"));
        } else {
            json_object *resp = make_response(req, 1, NULL);
            json_object *data = json_object_new_object();
            json_object *value = dots_config_get(cfg, path);
            json_object_object_add(data, "value",
                                   value ? json_object_get(value)
                                         : json_object_new_null());
            json_object_object_add(resp, "data", data);
            send_response(fd, resp);
        }
    } else if (strcmp(cmd, "shutdown") == 0) {
        json_object *resp = make_response(req, 1, NULL);
        send_response(fd, resp);
        running = 0;
    } else if (strcmp(cmd, "panel") == 0) {
        json_object *target_obj = NULL, *action_obj = NULL;
        const char *target = NULL, *action = NULL;
        if (json_object_object_get_ex(req, "target", &target_obj) &&
            json_object_is_type(target_obj, json_type_string))
            target = json_object_get_string(target_obj);
        if (json_object_object_get_ex(req, "action", &action_obj) &&
            json_object_is_type(action_obj, json_type_string))
            action = json_object_get_string(action_obj);
        if (!target || !target[0] || !action || !action[0]) {
            send_response(fd, make_response(req, 0, "missing target/action"));
        } else if (spawn_qs_panel(target, action) < 0) {
            send_response(fd, make_response(req, 0, "cannot spawn qs"));
        } else {
            json_object *resp = make_response(req, 1, NULL);
            add_data_string(resp, "delegated", "quickshell");
            send_response(fd, resp);
        }
    } else if (strcmp(cmd, "toggle") == 0 ||
               strcmp(cmd, "open") == 0 ||
               strcmp(cmd, "close") == 0) {
        /* Panel ownership belum pindah; jangan menipu caller. */
        send_response(fd, make_response(req, 0,
                       "panel command belum diimplementasikan di dotsd"));
    } else {
        send_response(fd, make_response(req, 0, "unknown command"));
    }
    json_object_put(req);
}

static void close_client(struct client *clients, size_t *count, size_t index) {
    close(clients[index].fd);
    clients[index] = clients[*count - 1];
    (*count)--;
}

static int accept_client(struct client *clients, size_t *count) {
    int fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        return -1;
    if (*count >= MAX_CLIENTS) {
        const char *msg = "{\"ok\":false,\"error\":\"too many clients\"}\n";
        (void)dots_ipc_send_json(fd, msg, strlen(msg));
        close(fd);
        return -1;
    }
    clients[*count].fd = fd;
    clients[*count].used = 0;
    (*count)++;
    return 0;
}

static void read_client(struct dots_config *cfg, time_t started,
                        struct client *clients, size_t *count, size_t index) {
    struct client *c = &clients[index];
    char tmp[8192];
    ssize_t n = recv(c->fd, tmp, sizeof tmp, 0);
    if (n == 0) {
        close_client(clients, count, index);
        return;
    }
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        close_client(clients, count, index);
        return;
    }
    if (c->used + (size_t)n >= sizeof c->buf) {
        const char *msg = "{\"ok\":false,\"error\":\"request too large\"}\n";
        (void)dots_ipc_send_json(c->fd, msg, strlen(msg));
        close_client(clients, count, index);
        return;
    }
    memcpy(c->buf + c->used, tmp, (size_t)n);
    c->used += (size_t)n;

    char line[DOTS_IPC_MAX];
    ssize_t got;
    while ((got = dots_ipc_take_line(c->buf, &c->used, line, sizeof line)) > 0) {
        handle_request(cfg, started, c->fd, line);
        if (!running)
            return;
    }
    if (got < 0)
        close_client(clients, count, index);
}

int main(void) {
    dots_log_init_from_env();
    umask(077);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    struct dots_config cfg;
    dots_config_init(&cfg);
    if (dots_config_load(&cfg) < 0)
        DOTS_WARN("config belum tersedia; dotsd tetap berjalan");

    if (setup_socket() < 0) {
        dots_config_destroy(&cfg);
        cleanup();
        return 1;
    }

    DOTS_INFO("dotsd %s siap di %s", DOTS_VERSION, socket_path);
    time_t started = time(NULL);
    time_t last_config_check = started;
    struct client clients[MAX_CLIENTS];
    size_t client_count = 0;

    while (running) {
        struct pollfd pfds[MAX_CLIENTS + 1];
        size_t nfds = 0;
        pfds[nfds].fd = listen_fd;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;
        for (size_t i = 0; i < client_count; i++) {
            pfds[nfds].fd = clients[i].fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int pr = poll(pfds, nfds, 500);
        if (pr < 0 && errno != EINTR) {
            DOTS_ERROR("poll: %s", strerror(errno));
            break;
        }
        if (pr > 0) {
            if (pfds[0].revents & POLLIN)
                (void)accept_client(clients, &client_count);
            /* Iterasi mundur supaya swap-remove aman. */
            for (size_t i = client_count; i > 0; i--) {
                size_t idx = i - 1;
                short rev = 0;
                for (size_t p = 1; p < nfds; p++) {
                    if (pfds[p].fd == clients[idx].fd) {
                        rev = pfds[p].revents;
                        break;
                    }
                }
                if (rev & (POLLIN | POLLHUP | POLLERR))
                    read_client(&cfg, started, clients, &client_count, idx);
                if (!running)
                    break;
            }
        }
        if (time(NULL) - last_config_check >= 2) {
            dots_config_reload_if_changed(&cfg);
            last_config_check = time(NULL);
        }
    }

    DOTS_INFO("dotsd berhenti");
    for (size_t i = 0; i < client_count; i++)
        close(clients[i].fd);
    dots_config_destroy(&cfg);
    cleanup();
    return 0;
}
