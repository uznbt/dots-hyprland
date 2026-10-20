#!/usr/bin/env bash
# ==============================================================================
# Script: monitor.sh
# Deskripsi: Menjalankan `nmcli monitor` sebagai subscriber perubahan jaringan.
#
# Masalah yang diselesaikan:
#   `nmcli monitor` berjalan terus-menerus dan tidak pernah keluar dengan
#   sendirinya. Quickshell TIDAK mematikan proses anaknya ketika shell
#   dimatikan (baik SIGTERM maupun SIGKILL), sehingga setiap reload / restart
#   / membuka aplikasi settings menyisakan proses yatim (~10 MB RSS tiap satu).
#   Script ini memastikan proses `nmcli monitor` selalu ikut mati bersama
#   pemiliknya, sehingga tidak ada penumpukan proses yatim.
#
# Cara kerja:
#   - `nmcli monitor` dijalankan sebagai anak dari script ini.
#   - Sebuah watchdog terpisah (setsid) memantau:
#       * PID parent  (quickshell) -> kalau quickshell mati, bersihkan.
#       * PID script ini            -> kalau script ini dibunuh, bersihkan.
#   - Watchdog memakai stdout/stderr sendiri, jadi output `nmcli monitor`
#     tetap diteruskan ke parser Quickshell lewat stdout script ini.
# ==============================================================================

set -u

# PID parent (quickshell) dan PID script ini.
owner_pid="$PPID"
self_pid="$$"

nmcli monitor &
monitor_pid=$!

# Watchdog terpisah: tidak ikut mati saat parent atau script ini dibunuh.
setsid bash -c '
    owner=$1
    self=$2
    target=$3
    while kill -0 "$owner" 2>/dev/null && kill -0 "$self" 2>/dev/null; do
        sleep 2
    done
    kill "$target" 2>/dev/null
    exit 0
' _ "$owner_pid" "$self_pid" "$monitor_pid" >/dev/null 2>&1 &

# Tunggu parent (quickshell) mati, lalu rapikan.
while kill -0 "$owner_pid" 2>/dev/null; do
    sleep 2
done

kill "$monitor_pid" 2>/dev/null
wait "$monitor_pid" 2>/dev/null
exit 0
