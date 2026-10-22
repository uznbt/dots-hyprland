#ifndef DOTS_IPC_H
#define DOTS_IPC_H

#include <stddef.h>
#include <sys/types.h>

#define DOTS_IPC_MAX 65536

/* Path socket daemon; lihat paths.h. */
int dots_ipc_client_connect(const char *path);

/* Kirim satu baris JSON (newline ditambahkan jika belum ada). */
int dots_ipc_send_line(int fd, const char *json);
int dots_ipc_send_json(int fd, const char *json, size_t len);

/* Baca satu baris ke buf, tanpa newline. Return panjang, 0 saat EOF, -1 error. */
ssize_t dots_ipc_recv_line(int fd, char *buf, size_t size);

/* Ambil satu baris penuh dari buffer; dipakai server nonblocking. */
ssize_t dots_ipc_take_line(char *buf, size_t *used, char *out, size_t out_size);

#endif
