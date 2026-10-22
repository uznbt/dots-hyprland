#!/bin/sh
# Smoke test untuk dotsd + dots-ctl. Tidak memerlukan Wayland.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD_DIR:-"$ROOT/build"}
TMP=$(mktemp -d)
trap 'pkill -f "$BUILD/dotsd" 2>/dev/null || true; rm -rf "$TMP"' EXIT INT TERM

export XDG_RUNTIME_DIR="$TMP/run"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

DOTS_LOG_LEVEL=error "$BUILD/dotsd" >"$TMP/dotsd.log" 2>&1 &
PID=$!

i=0
while [ ! -S "$XDG_RUNTIME_DIR/dots/daemon.sock" ]; do
    i=$((i + 1))
    if [ "$i" -gt 50 ]; then
        echo "dotsd tidak membuat socket" >&2
        cat "$TMP/dotsd.log" >&2
        exit 1
    fi
    sleep 0.05
done

ping=$("$BUILD/dots-ctl" ping)
echo "$ping" | grep -q '"ok":true'
echo "$ping" | grep -q '"name":"dotsd"'

status=$("$BUILD/dots-ctl" status)
echo "$status" | grep -q '"config_loaded"'

"$BUILD/dots-ctl" shutdown >/dev/null
wait "$PID" 2>/dev/null || true

echo "dotsd ipc smoke test: ok"
