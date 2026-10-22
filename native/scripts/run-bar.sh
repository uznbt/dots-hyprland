#!/bin/sh
# Jalankan bar native untuk uji coba, lalu kembalikan bar Quickshell saat keluar.
#
# Ini bukan pengganti sesi. Selama migrasi, Quickshell tetap menjadi fallback
# untuk sidebar/panel yang belum diport.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN="$ROOT/build/dots-bar"
QS_CONFIG=${DOTS_QS_CONFIG:-ii}
NAMESPACE=${DOTS_BAR_NAMESPACE:-dots:bar}

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
}
trap restore EXIT INT TERM HUP

# Tutup bar Quickshell supaya tidak berebut exclusive zone dengan bar native.
qs -c "$QS_CONFIG" ipc call bar close >/dev/null 2>&1 || true

# Layer rule hanya untuk namespace native; tidak mengubah rule Quickshell.
hyprctl eval "hl.layer_rule({ match = { namespace = \"$NAMESPACE\" }, blur = true })" >/dev/null 2>&1 || true
hyprctl eval "hl.layer_rule({ match = { namespace = \"$NAMESPACE\" }, ignore_alpha = 0.79 })" >/dev/null 2>&1 || true

"$BIN" "$@"
