#!/usr/bin/env bash
# ==============================================================================
# Script: electron-lowram.sh
# Deskripsi: Menjalankan aplikasi Chromium/Electron dengan batas memori rendah.
#
# Dipakai untuk: Brave, Spotify, WhatsZan
#
# Latar belakang:
#   Aplikasi Chromium/Electron membuat satu proses renderer per tab. Pada
#   mesin dengan RAM terbatas, tiga aplikasi seperti itu bisa memakan 4-5 GB
#   (Brave 2,7 GB + Spotify 1,1 GB + WhatsZan 0,9 GB di mesin 8 GB).
#   Flag di bawah membatasi jumlah proses renderer dan ukuran cache disk
#   sehingga pemakaian RAM turun drastis.
#
# Catatan hasil pengukuran di mesin ini (i5-8250U / 8 GB / Intel UHD 620):
#   - WhatsZan spends ~780 MB PSS saat ter-login. ~65%-nya ada di renderer
#     (WhatsApp Web itu sendiri) dan tidak bisa dikecilkan dari luar.
#   - Batas heap JS 512 MB -> 256 MB TIDAK mengubah pemakaian RAM, hanya
#     menambah frekuensi GC. Jadi batasnya dibiarkan 512 MB.
#   - Cache disk HTTP 128 MB -> 64 MB menghemat ~64 MB disk dan sedikit RAM
#     di network service.
#   - Batas renderer 8 -> 2: WhatsZan hanya memakai 1 renderer untuk
#     WhatsApp Web, sisanya adalah zygote/GPU/service worker.
#
# Pemakaian:
#   electron-lowram.sh <path-binary> [argumen-asli...]
#
# Sumber nilai batas (dari yang paling tinggi):
#   1. Variabel lingkungan (LOWRAM_RENDERER_LIMIT, LOWRAM_JS_HEAP_MB,
#      LOWRAM_DISK_CACHE)
#   2. Pengaturan shell: ~/.config/illogical-impulse/config.json
#      -> performance.memory.{rendererLimit,jsHeapMb,diskCacheMb}
#      (dapat diubah dari halaman "Performance" di aplikasi pengaturan)
#   3. Nilai bawaan di bawah
#
# Catatan:
#   Flag hanya berlaku untuk instance yang baru START. Untuk menerapkan
#   perubahan, aplikasi harus ditutup penuh (bukan hanya window-nya).
#   Jika aplikasi sudah berjalan, perintah ini hanya membuka window baru
#   di instance yang sudah ada dan flag diabaikan.
# ==============================================================================

set -e

if [[ $# -lt 1 ]]; then
    echo "electron-lowram:Usage: electron-lowram.sh <binary> [args...]" >&2
    exit 1
fi

BIN="$1"
shift

DEFAULT_RENDERER_LIMIT=2
DEFAULT_JS_HEAP_MB=512
DEFAULT_DISK_CACHE_MB=64

CONFIG_FILE="${XDG_CONFIG_HOME:-${HOME}/.config}/illogical-impulse/config.json"

# Membaca nilai dari config.json tanpa bergantung pada jq (python3 hampir
# selalu ada).
read_config_value() {
    local key="$1"
    [[ -r "${CONFIG_FILE}" ]] || return 0

    if command -v jq >/dev/null 2>&1; then
        jq -r ".performance.memory.${key} // empty" "${CONFIG_FILE}" 2>/dev/null || true
    elif command -v python3 >/dev/null 2>&1; then
        python3 - "$CONFIG_FILE" "$key" <<'PY' 2>/dev/null || true
import json
import sys

try:
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        value = json.load(handle).get("performance", {}).get("memory", {}).get(sys.argv[2])
except Exception:
    value = None

if isinstance(value, (int, float)) and value > 0:
    print(int(value))
PY
    fi
}

config_value() {
    local key="$1"
    local value
    value="$(read_config_value "${key}")"
    if [[ -n "${value}" && "${value}" =~ ^[0-9]+$ ]]; then
        echo "${value}"
    fi
}

RENDERER_LIMIT="${LOWRAM_RENDERER_LIMIT:-$(config_value rendererLimit)}"
JS_HEAP_MB="${LOWRAM_JS_HEAP_MB:-$(config_value jsHeapMb)}"
DISK_CACHE_MB="${LOWRAM_DISK_CACHE_MB:-$(config_value diskCacheMb)}"

RENDERER_LIMIT="${RENDERER_LIMIT:-${DEFAULT_RENDERER_LIMIT}}"
JS_HEAP_MB="${JS_HEAP_MB:-${DEFAULT_JS_HEAP_MB}}"
DISK_CACHE_MB="${DISK_CACHE_MB:-${DEFAULT_DISK_CACHE_MB}}"

DISK_CACHE=$((DISK_CACHE_MB * 1024 * 1024))

if [[ ! -x "${BIN}" ]]; then
    resolved="$(command -v "${BIN}" 2>/dev/null || true)"
    if [[ -n "${resolved}" ]]; then
        BIN="${resolved}"
    else
        echo "electron-lowram: tidak menemukan binary '${BIN}'" >&2
        exit 1
    fi
fi

exec "${BIN}" \
    --renderer-process-limit="${RENDERER_LIMIT}" \
    --js-flags="--max-old-space-size=${JS_HEAP_MB}" \
    --disk-cache-size="${DISK_CACHE}" \
    "$@"
