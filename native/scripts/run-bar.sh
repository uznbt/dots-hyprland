#!/bin/sh
# Jalankan bar native untuk uji coba, lalu kembalikan bar Quickshell saat keluar.
#
# Ini bukan pengganti sesi. Selama migrasi, Quickshell tetap menjadi fallback
# untuk sidebar/panel yang belum diport.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN="$ROOT/build/dots-bar"
DAEMON="$ROOT/build/dotsd"
CTL="$ROOT/build/dots-ctl"
QS_CONFIG=${DOTS_QS_CONFIG:-ii}
NAMESPACE=${DOTS_BAR_NAMESPACE:-dots:bar}
DAEMON_PID=""

if [ ! -x "$BIN" ]; then
    echo "dots-bar belum dibangun. Jalankan: meson setup build native && meson compile -C native/build" >&2
    exit 1
fi

export DOTS_ASSET_DIR=${DOTS_ASSET_DIR:-"$ROOT/build"}
export DOTS_BAR_NAMESPACE="$NAMESPACE"
export DOTS_QS_CONFIG="$QS_CONFIG"

restore() {
    pkill -x dots-bar 2>/dev/null || true
    qs -c "$QS_CONFIG" ipc call bar open >/dev/null 2>&1 || true
    if [ -n "$DAEMON_PID" ]; then
        "$CTL" shutdown >/dev/null 2>&1 || kill "$DAEMON_PID" 2>/dev/null || true
    fi
}
trap restore EXIT INT TERM HUP

# Nyalakan daemon kalau belum ada. Bar akan memakai dotsd untuk delegasi aksi
# panel ke Quickshell; kalau daemon tidak tersedia, bar fallback langsung.
if ! "$CTL" ping >/dev/null 2>&1; then
    DOTS_LOG_LEVEL=${DOTS_LOG_LEVEL:-info} "$DAEMON" >>"$ROOT/build/dotsd.log" 2>&1 &
    DAEMON_PID=$!
    i=0
    while ! "$CTL" ping >/dev/null 2>&1; do
        i=$((i + 1))
        if [ "$i" -gt 40 ]; then
            echo "dotsd tidak siap; bar akan memakai fallback qs" >&2
            DAEMON_PID=""
            break
        fi
        sleep 0.05
    done
fi

# Tutup bar Quickshell supaya tidak berebut exclusive zone dengan bar native.
qs -c "$QS_CONFIG" ipc call bar close >/dev/null 2>&1 || true

# Layer rule hanya untuk namespace native; tidak mengubah rule Quickshell.
hyprctl eval "hl.layer_rule({ match = { namespace = \"$NAMESPACE\" }, blur = true })" >/dev/null 2>&1 || true
hyprctl eval "hl.layer_rule({ match = { namespace = \"$NAMESPACE\" }, ignore_alpha = 0.79 })" >/dev/null 2>&1 || true

"$BIN" "$@"
