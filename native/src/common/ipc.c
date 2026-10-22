#include "common/ipc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int dots_ipc_client_connect(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    int n = snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    if (n < 0 || (size_t)n >= sizeof addr.sun_path) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int dots_ipc_send_json(int fd, const char *json, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, json + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

int dots_ipc_send_line(int fd, const char *json) {
    size_t len = strlen(json);
    if (len == 0)
        return -1;
    if (json[len - 1] == '\n')
        return dots_ipc_send_json(fd, json, len);
    if (dots_ipc_send_json(fd, json, len) < 0)
        return -1;
    char nl = '\n';
    return dots_ipc_send_json(fd, &nl, 1);
}

ssize_t dots_ipc_recv_line(int fd, char *buf, size_t size) {
    if (size < 2) {
        errno = EINVAL;
        return -1;
    }
    size_t used = 0;
    while (used + 1 < size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        if (c == '\n')
            break;
        buf[used++] = c;
    }
    buf[used] = '\0';
    return (ssize_t)used;
}

ssize_t dots_ipc_take_line(char *buf, size_t *used, char *out, size_t out_size) {
    if (!buf || !used || !out || out_size == 0)
        return -1;
    char *nl = memchr(buf, '\n', *used);
    if (!nl)
        return 0;

    size_t len = (size_t)(nl - buf);
    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, buf, copy);
    out[copy] = '\0';

    size_t consumed = len + 1;
    memmove(buf, buf + consumed, *used - consumed);
    *used -= consumed;
    return (ssize_t)copy;
}
