#ifndef DOTS_PATHS_H
#define DOTS_PATHS_H

#include <stddef.h>

/* Semua fungsi mengembalikan 0 jika berhasil, -1 jika buffer terlalu kecil. */

int dots_runtime_dir(char *buf, size_t size);
int dots_socket_path(char *buf, size_t size);
int dots_pid_path(char *buf, size_t size);
int dots_config_path(char *buf, size_t size);
int dots_state_dir(char *buf, size_t size);
int dots_cache_dir(char *buf, size_t size);

/* Buat direktori (beserta parent) dengan mode 0700. */
int dots_mkdir_p(const char *path, unsigned int mode);

#endif
