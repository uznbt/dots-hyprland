#!/usr/bin/env bash
# ==============================================================================
# Script: install-desktop-overrides.sh
# Deskripsi: Memasang override .desktop milik user (level ~/.local) untuk
#            Brave, Spotify, dan WhatsZan.
#
# Kenapa perlu override:
#   Override di ~/.local/share/applications/ punya prioritas lebih tinggi
#   daripada /usr/share/applications, jadi kita bisa menambahkan flag
#   pembatas memori tanpa sudo dan tanpa mengubah file sistem.
#   Lihat docs/RAM.md untuk detailnya.
#
# Catatan:
#   File .desktop di repo ini memakai path absolut ke home directory.
#   Jalankan ulang script ini kalau home directory berubah.
# ==============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${REPO_ROOT}/dots-extra/desktop-overrides"
DEST_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"

if [[ ! -d "${SRC_DIR}" ]]; then
    echo "install-desktop-overrides: ${SRC_DIR} tidak ditemukan" >&2
    exit 1
fi

mkdir -p "${DEST_DIR}"

for file in "${SRC_DIR}"/*.desktop; do
    name="$(basename "${file}")"
    echo "Memasang override: ${DEST_DIR}/${name}"
    sed "s|/home/uznbt|${HOME}|g" "${file}" > "${DEST_DIR}/${name}"
done

echo "Selesai. Restart aplikasi yang sudah berjalan agar flag baru dipakai."
