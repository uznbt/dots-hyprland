#!/bin/sh
# Ukur PSS/RSS/anon sebuah proses native.
set -eu
name=${1:-dots-bar}
pid=$(pgrep -x "$name" | head -1 || true)
if [ -z "$pid" ]; then
    echo "$name tidak berjalan" >&2
    exit 1
fi
echo "process: $name (pid $pid)"
awk '/^(Pss|Rss|Anonymous|Private_Dirty):/{print}' "/proc/$pid/smaps_rollup"
