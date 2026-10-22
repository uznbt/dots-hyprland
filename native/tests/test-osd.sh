#!/bin/sh
# Smoke test untuk dots-osd yang tidak memerlukan Wayland.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD_DIR:-"$ROOT/build"}

out=$("$BUILD/dots-osd" --help)
echo "$out" | grep -q 'usage: dots-osd'
echo "$out" | grep -q 'show volume|brightness'

# Validasi argumen harus gagal tanpa menyentuh Wayland.
if "$BUILD/dots-osd" show volume 2 >/dev/null 2>&1; then
    echo "dots-osd menerima nilai di luar 0..1" >&2
    exit 1
fi
if "$BUILD/dots-osd" brightness mute >/dev/null 2>&1; then
    echo "dots-osd menerima brightness mute" >&2
    exit 1
fi

echo "dots-osd smoke test: ok"
