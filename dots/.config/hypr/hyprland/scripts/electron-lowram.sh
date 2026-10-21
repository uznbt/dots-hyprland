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
#   Flag di bawah membatasi jumlah proses renderer dan ukuran heap
#   JavaScript per renderer sehingga pemakaian RAM turun drastis, dengan
#   homozygous kecil pada tab yang lama tidak aktif.
#
# Pemakaian:
#   electron-lowram.sh <path-binary> [argumen-asli...]
#
# Variabel lingkungan (opsional):
#   LOWRAM_RENDERER_LIMIT  jumlah proses renderer maks  (default: 8)
#   LOWRAM_JS_HEAP_MB      batas heap JS per renderer MB  (default: 512)
#   LOWRAM_DISK_CACHE      ukuran cache disk dalam byte  (default: 134217728)
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

RENDERER_LIMIT="${LOWRAM_RENDERER_LIMIT:-8}"
JS_HEAP_MB="${LOWRAM_JS_HEAP_MB:-512}"
DISK_CACHE="${LOWRAM_DISK_CACHE:-134217728}"

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
