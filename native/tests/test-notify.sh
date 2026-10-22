#!/bin/sh
# Smoke test server notifikasi D-Bus tanpa Wayland.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD_DIR:-"$ROOT/build"}

if ! command -v dbus-daemon >/dev/null 2>&1 ||
   ! command -v gdbus >/dev/null 2>&1 ||
   ! command -v dbus-monitor >/dev/null 2>&1; then
    echo "test-notify: dbus tools tidak tersedia; lewati"
    exit 0
fi

TMP=$(mktemp -d)
BUS_PID=
NOTIFY_PID=
MONITOR_PID=
cleanup() {
    [ -n "${MONITOR_PID:-}" ] && kill "$MONITOR_PID" 2>/dev/null || true
    [ -n "${NOTIFY_PID:-}" ] && kill "$NOTIFY_PID" 2>/dev/null || true
    [ -n "${BUS_PID:-}" ] && kill "$BUS_PID" 2>/dev/null || true
    wait "$NOTIFY_PID" 2>/dev/null || true
    wait "$BUS_PID" 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

ADDR="unix:path=$TMP/bus"
DBUS_SESSION_BUS_ADDRESS="$ADDR" dbus-daemon --session --address="$ADDR" \
    --nofork --nopidfile >"$TMP/bus.log" 2>&1 &
BUS_PID=$!
for _ in $(seq 1 50); do
    [ -S "$TMP/bus" ] && break
    sleep 0.05
done

DOTS_LOG_LEVEL=error DBUS_SESSION_BUS_ADDRESS="$ADDR" \
    "$BUILD/dots-notify" serve --no-ui >"$TMP/notify.log" 2>&1 &
NOTIFY_PID=$!

info=
for _ in $(seq 1 80); do
    if info=$(DBUS_SESSION_BUS_ADDRESS="$ADDR" gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.GetServerInformation \
        2>/dev/null); then
        break
    fi
    sleep 0.05
done
if [ -z "$info" ]; then
    echo "test-notify: server tidak mengambil nama D-Bus" >&2
    cat "$TMP/notify.log" >&2
    exit 1
fi
echo "$info" | grep -q "'dots-notify'"
echo "$info" | grep -q "'1.2'"

caps=$(DBUS_SESSION_BUS_ADDRESS="$ADDR" gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.GetCapabilities)
echo "$caps" | grep -q "'actions'"
echo "$caps" | grep -q "'body'"

call_notify() {
    DBUS_SESSION_BUS_ADDRESS="$ADDR" gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.Notify \
        "$@"
}

out=$(call_notify 'TestApp' 0 'dialog-information' 'Hello' 'Body' \
    "['default', 'Open']" '{}' 0)
echo "$out" | grep -q '(uint32 1,)'

# replaces_id harus mempertahankan ID yang sama.
out=$(call_notify 'TestApp' 1 'dialog-information' 'Hello again' \
    'Updated body' "[]" '{}' 0)
echo "$out" | grep -q '(uint32 1,)'

DBUS_SESSION_BUS_ADDRESS="$ADDR" timeout 4 dbus-monitor --session \
    "type='signal',interface='org.freedesktop.Notifications'" \
    >"$TMP/monitor.log" 2>&1 &
MONITOR_PID=$!
sleep 0.25

DBUS_SESSION_BUS_ADDRESS="$ADDR" gdbus call --session \
    --dest org.freedesktop.Notifications \
    --object-path /org/freedesktop/Notifications \
    --method org.freedesktop.Notifications.CloseNotification 1 >/dev/null
sleep 0.25
awk '/member=NotificationClosed/ { getline; id=$2; getline; reason=$2;
     if (id == 1 && reason == 3) found=1 } END { exit !found }' \
    "$TMP/monitor.log"

# Timeout harus menghasilkan reason 1 (expired) untuk ID 2.
call_notify 'TimeoutApp' 0 'dialog-information' 'Short' 'Bye' \
    "[]" '{}' 200 >/dev/null
sleep 0.6
awk '/member=NotificationClosed/ { getline; id=$2; getline; reason=$2;
     if (id == 2 && reason == 1) found=1 } END { exit !found }' \
    "$TMP/monitor.log"

kill "$MONITOR_PID" 2>/dev/null || true
wait "$MONITOR_PID" 2>/dev/null || true
MONITOR_PID=

echo "dots-notify smoke test: ok"
