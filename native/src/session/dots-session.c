#define _GNU_SOURCE
#include "common/ipc.h"
#include "common/log.h"
#include "common/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define SESSION_LOCK "session.lock"
#define SESSION_PID "session.pid"
#define BAR_PID "bar.pid"

static volatile sig_atomic_t running = 1;
static char self_dir[PATH_MAX];

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static void usage(FILE *out) {
    fprintf(out,
        "usage: dots-session <command>\n"
        "\n"
        "commands:\n"
        "  start                 start hybrid native bar (foreground)\n"
        "  stop                  stop session and reopen Quickshell bar\n"
        "  status                show backend/daemon/bar status\n"
        "  backend [name]        show or set backend: hybrid, quickshell, native\n"
        "  version\n"
        "\n"
        "Mode native belum tersedia; gunakan hybrid selama Quickshell masih\n"
        "menangani panel yang belum diport.\n");
}

static int join_path(char *out, size_t size, const char *a, const char *b) {
    int n = snprintf(out, size, "%s/%s", a, b);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static void init_self_dir(void) {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) {
        snprintf(self_dir, sizeof self_dir, ".");
        return;
    }
    buf[n] = '\0';
    char *slash = strrchr(buf, '/');
    if (slash)
        *slash = '\0';
    snprintf(self_dir, sizeof self_dir, "%s", buf);
}

static const char *qs_config(void) {
    const char *config = getenv("DOTS_QS_CONFIG");
    return (config && config[0]) ? config : "ii";
}

static int runtime_subpath(char *out, size_t size, const char *name) {
    char dir[768];
    if (dots_runtime_dir(dir, sizeof dir) < 0)
        return -1;
    if (dots_mkdir_p(dir, 0700) < 0)
        return -1;
    return join_path(out, size, dir, name);
}

static int state_backend_path(char *out, size_t size) {
    char dir[768];
    if (dots_state_dir(dir, sizeof dir) < 0)
        return -1;
    if (dots_mkdir_p(dir, 0700) < 0)
        return -1;
    return join_path(out, size, dir, "backend");
}

static int read_backend(char *out, size_t size) {
    snprintf(out, size, "hybrid");
    char path[1024];
    if (state_backend_path(path, sizeof path) < 0)
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[32];
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0])
            snprintf(out, size, "%s", line);
    }
    fclose(f);
    return 0;
}

static int write_backend(const char *value) {
    char path[1024];
    if (state_backend_path(path, sizeof path) < 0)
        return -1;
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s.tmp.%ld", path, (long)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    int ok = fprintf(f, "%s\n", value) > 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok || rename(tmp, path) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static pid_t read_pid_file(const char *name) {
    char path[1024];
    if (runtime_subpath(path, sizeof path, name) < 0)
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    long pid = 0;
    if (fscanf(f, "%ld", &pid) != 1)
        pid = 0;
    fclose(f);
    return pid > 0 ? (pid_t)pid : 0;
}

static void write_pid_file(const char *name, pid_t pid) {
    char path[1024];
    if (runtime_subpath(path, sizeof path, name) < 0)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "%ld\n", (long)pid);
    fclose(f);
}

static void remove_runtime_file(const char *name) {
    char path[1024];
    if (runtime_subpath(path, sizeof path, name) == 0)
        unlink(path);
}

static int process_alive(pid_t pid) {
    if (pid <= 0)
        return 0;
    return kill(pid, 0) == 0 || errno == EPERM;
}

static int ipc_command(const char *cmd, int want_ok) {
    char sock[1024];
    if (dots_socket_path(sock, sizeof sock) < 0)
        return -1;
    int fd = dots_ipc_client_connect(sock);
    if (fd < 0)
        return -1;

    char req[512];
    int n = snprintf(req, sizeof req, "{\"id\":1,\"cmd\":\"%s\"}", cmd);
    int rc = -1;
    if (n > 0 && (size_t)n < sizeof req && dots_ipc_send_line(fd, req) == 0) {
        char response[DOTS_IPC_MAX];
        if (dots_ipc_recv_line(fd, response, sizeof response) > 0) {
            if (want_ok)
                rc = strstr(response, "\"ok\":true") ? 0 : -1;
            else
                rc = 0;
        }
    }
    close(fd);
    return rc;
}

static int run_qs(const char *target, const char *action) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execlp("qs", "qs", "-c", qs_config(), "ipc", "call",
               target, action, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int find_executable(const char *name, char *out, size_t size) {
    if (join_path(out, size, self_dir, name) == 0 && access(out, X_OK) == 0)
        return 0;
    const char *path = getenv("PATH");
    if (!path)
        return -1;
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", path);
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir;
         dir = strtok_r(NULL, ":", &save)) {
        if (join_path(out, size, dir, name) == 0 && access(out, X_OK) == 0)
            return 0;
    }
    return -1;
}

static void setup_asset_dir(void) {
    if (getenv("DOTS_ASSET_DIR"))
        return;
    char dir[PATH_MAX];
    if (join_path(dir, sizeof dir, self_dir, "assets") == 0 &&
        access(dir, R_OK) == 0) {
        setenv("DOTS_ASSET_DIR", dir, 1);
        return;
    }
    const char *data = getenv("XDG_DATA_HOME");
    char base[PATH_MAX];
    if (data && data[0]) {
        if (join_path(base, sizeof base, data, "dots/assets") == 0 &&
            access(base, R_OK) == 0) {
            setenv("DOTS_ASSET_DIR", base, 1);
            return;
        }
    }
    const char *home = getenv("HOME");
    if (home) {
        int n = snprintf(base, sizeof base, "%s/.local/share/dots/assets", home);
        if (n > 0 && (size_t)n < sizeof base && access(base, R_OK) == 0) {
            setenv("DOTS_ASSET_DIR", base, 1);
            return;
        }
    }
    if (access("/usr/share/dots/assets", R_OK) == 0)
        setenv("DOTS_ASSET_DIR", "/usr/share/dots/assets", 1);
}

static int start_daemon(void) {
    if (ipc_command("ping", 1) == 0)
        return 0;

    char exe[PATH_MAX];
    if (find_executable("dotsd", exe, sizeof exe) < 0) {
        DOTS_ERROR("dotsd tidak ditemukan");
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl(exe, exe, (char *)NULL);
        _exit(127);
    }

    for (int i = 0; i < 60; i++) {
        if (ipc_command("ping", 1) == 0)
            return 0;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 50 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    DOTS_ERROR("dotsd tidak siap setelah 3 detik");
    kill(pid, SIGTERM);
    return -1;
}

static pid_t start_bar(void) {
    char exe[PATH_MAX];
    if (find_executable("dots-bar", exe, sizeof exe) < 0) {
        DOTS_ERROR("dots-bar tidak ditemukan");
        return 0;
    }
    setup_asset_dir();
    if (!getenv("DOTS_BAR_NAMESPACE"))
        setenv("DOTS_BAR_NAMESPACE", "dots:bar", 1);
    if (!getenv("DOTS_QS_CONFIG"))
        setenv("DOTS_QS_CONFIG", qs_config(), 1);

    pid_t pid = fork();
    if (pid < 0)
        return 0;
    if (pid == 0) {
        execl(exe, exe, (char *)NULL);
        _exit(127);
    }
    write_pid_file(BAR_PID, pid);
    return pid;
}

static void stop_bar(void) {
    pid_t pid = read_pid_file(BAR_PID);
    if (process_alive(pid))
        kill(pid, SIGTERM);
    remove_runtime_file(BAR_PID);
}

static void reopen_qs_bar(void) {
    (void)run_qs("bar", "open");
}

static int cmd_start(void) {
    char backend[32];
    read_backend(backend, sizeof backend);
    if (strcmp(backend, "quickshell") == 0) {
        printf("backend quickshell: tidak menjalankan bar native\n");
        reopen_qs_bar();
        return 0;
    }
    if (strcmp(backend, "native") == 0) {
        fprintf(stderr,
                "dots-session: mode native belum tersedia; gunakan hybrid\n");
        return 1;
    }
    if (strcmp(backend, "hybrid") != 0) {
        fprintf(stderr, "dots-session: backend tidak dikenal: %s\n", backend);
        return 1;
    }

    char lock_path[1024];
    if (runtime_subpath(lock_path, sizeof lock_path, SESSION_LOCK) < 0)
        return 1;
    int lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        fprintf(stderr, "dots-session: sudah ada sesi hybrid berjalan\n");
        if (lock_fd >= 0)
            close(lock_fd);
        return 1;
    }

    write_pid_file(SESSION_PID, getpid());
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int daemon_was_running = ipc_command("ping", 1) == 0;
    if (start_daemon() < 0) {
        remove_runtime_file(SESSION_PID);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return 1;
    }

    (void)run_qs("bar", "close");
    pid_t bar_pid = start_bar();
    if (!bar_pid) {
        DOTS_ERROR("gagal menjalankan dots-bar");
        if (!daemon_was_running)
            (void)ipc_command("shutdown", 1);
        reopen_qs_bar();
        remove_runtime_file(SESSION_PID);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return 1;
    }

    printf("dots-session hybrid: bar pid %ld, backend %s\n",
           (long)bar_pid, backend);
    fflush(stdout);

    int status = 0;
    while (running) {
        pid_t r = waitpid(bar_pid, &status, WNOHANG);
        if (r == bar_pid)
            break;
        if (r < 0 && errno != EINTR)
            break;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 200 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }

    if (process_alive(bar_pid))
        kill(bar_pid, SIGTERM);
    remove_runtime_file(BAR_PID);
    reopen_qs_bar();
    if (!daemon_was_running)
        (void)ipc_command("shutdown", 1);
    remove_runtime_file(SESSION_PID);
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    return 0;
}

static int cmd_stop(void) {
    pid_t session = read_pid_file(SESSION_PID);
    if (process_alive(session)) {
        kill(session, SIGTERM);
        for (int i = 0; i < 30; i++) {
            if (!process_alive(session))
                break;
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
    }
    stop_bar();
    reopen_qs_bar();
    remove_runtime_file(SESSION_PID);
    printf("dots-session: dihentikan; bar Quickshell dibuka kembali\n");
    return 0;
}

static int cmd_status(void) {
    char backend[32];
    read_backend(backend, sizeof backend);
    pid_t bar = read_pid_file(BAR_PID);
    pid_t session = read_pid_file(SESSION_PID);
    int daemon = ipc_command("ping", 1) == 0;
    printf("backend : %s\n", backend);
    printf("session : %s", process_alive(session) ? "running" : "stopped");
    if (process_alive(session))
        printf(" (pid %ld)", (long)session);
    printf("\n");
    printf("dotsd   : %s\n", daemon ? "running" : "stopped");
    printf("dots-bar: %s", process_alive(bar) ? "running" : "stopped");
    if (process_alive(bar))
        printf(" (pid %ld)", (long)bar);
    printf("\n");
    return 0;
}

static int cmd_backend(int argc, char **argv) {
    char current[32];
    read_backend(current, sizeof current);
    if (argc < 3) {
        printf("%s\n", current);
        return 0;
    }
    const char *value = argv[2];
    if (strcmp(value, "hybrid") != 0 && strcmp(value, "quickshell") != 0 &&
        strcmp(value, "native") != 0) {
        fprintf(stderr, "backend harus: hybrid | quickshell | native\n");
        return 2;
    }
    if (write_backend(value) < 0) {
        fprintf(stderr, "gagal menulis backend: %s\n", strerror(errno));
        return 1;
    }
    printf("backend disimpan: %s\n", value);
    if (strcmp(value, "quickshell") == 0)
        reopen_qs_bar();
    return 0;
}

int main(int argc, char **argv) {
    dots_log_init_from_env();
    init_self_dir();

    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "start") == 0)
        return cmd_start();
    if (strcmp(cmd, "stop") == 0)
        return cmd_stop();
    if (strcmp(cmd, "status") == 0)
        return cmd_status();
    if (strcmp(cmd, "backend") == 0)
        return cmd_backend(argc, argv);
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("dots-session %s\n", VERSION);
        return 0;
    }
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        usage(stdout);
        return 0;
    }
    usage(stderr);
    return 2;
}
