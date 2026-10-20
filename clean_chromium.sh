#!/usr/bin/env bash
# ==============================================================================
# Script: clean_chromium.sh
# Deskripsi: Menghapus seluruh data, profile, cache, dan meng-uninstall
#            Ungoogled Chromium (ungoogled-chromium-bin) di Arch / CachyOS.
# ==============================================================================

set -eo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${CYAN}${BOLD}====================================================${NC}"
echo -e "${CYAN}${BOLD}   PEMBERSIH & UNINSTALL UNGOOGLED CHROMIUM         ${NC}"
echo -e "${CYAN}${BOLD}====================================================${NC}"

# 1. Hentikan semua proses Chromium yang aktif
echo -e "\n${BLUE}[1/4] Menghentikan semua proses Chromium aktif...${NC}"
pkill -9 -f "/usr/bin/chromium" 2>/dev/null || true
pkill -9 -f "ungoogled-chromium" 2>/dev/null || true
echo -e "${GREEN}✓ Proses Chromium telah dihentikan.${NC}"

# 2. Daftar target folder data
DATA_PATHS=(
    "$HOME/.config/chromium"
    "$HOME/.cache/chromium"
    "$HOME/.var/app/io.github.ungoogled_software.ungoogled_chromium"
    "$HOME/.var/app/org.chromium.Chromium"
    "$HOME/.cache/fish/generated_completions/chromium.fish"
)

echo -e "\n${BLUE}[2/4] Memindai berkas dan data Ungoogled Chromium...${NC}"
FOUND_ANY=false
for target in "${DATA_PATHS[@]}"; do
    if [ -e "$target" ]; then
        FOUND_ANY=true
        SIZE=$(du -sh "$target" 2>/dev/null | cut -f1)
        echo -e "  - ${YELLOW}Ditemukan:${NC} $target (${BOLD}$SIZE${NC})"
    fi
done

# File temp
TMP_FILES=$(find /tmp /dev/shm -maxdepth 2 -iname "*org.chromium.Chromium*" -user "$USER" 2>/dev/null || true)
if [ -n "$TMP_FILES" ]; then
    while IFS= read -r f; do
        if [ -n "$f" ]; then
            FOUND_ANY=true
            echo -e "  - ${YELLOW}File temp:${NC} $f"
        fi
    done <<< "$TMP_FILES"
fi

if [ "$FOUND_ANY" = false ]; then
    echo -e "${YELLOW}Tidak ditemukan direktori data Chromium di sistem.${NC}"
fi

# Konfirmasi jika bukan flag -y
if [[ "$1" != "-y" && "$1" != "--yes" ]]; then
    echo -e "\n${RED}${BOLD}PERINGATAN:${NC} Tindakan ini akan menghapus ${BOLD}SEMUA${NC} profil browser,"
    echo -e "riwayat, ekstensi, bookmark, dan cache Ungoogled Chromium secara ${BOLD}PERMANEN${NC}!"
    read -p "Apakah Anda yakin ingin melanjutkan? (y/N): " -r CONFIRM
    if [[ ! "$CONFIRM" =~ ^[yY]([eE][sS])?$ ]]; then
        echo -e "${YELLOW}Operasi dibatalkan.${NC}"
        exit 0
    fi
fi

# 3. Hapus seluruh data user
echo -e "\n${BLUE}[3/4] Menghapus seluruh data & cache Chromium...${NC}"
for target in "${DATA_PATHS[@]}"; do
    if [ -e "$target" ]; then
        rm -rf "$target"
        echo -e "  ${GREEN}✓ Berhasil dihapus:${NC} $target"
    fi
done

if [ -n "$TMP_FILES" ]; then
    while IFS= read -r f; do
        if [ -n "$f" ] && [ -e "$f" ]; then
            rm -rf "$f"
            echo -e "  ${GREEN}✓ Berhasil dihapus:${NC} $f"
        fi
    done <<< "$TMP_FILES"
fi

# 4. Uninstall paket ungoogled-chromium-bin
echo -e "\n${BLUE}[4/4] Meng-uninstall paket ungoogled-chromium-bin...${NC}"
if pacman -Qs "ungoogled-chromium-bin" >/dev/null 2>&1; then
    echo -e "${YELLOW}Menjalankan pacman untuk menghapus paket dan dependensi yang tidak terpakai...${NC}"
    sudo pacman -Rns --noconfirm ungoogled-chromium-bin
    echo -e "${GREEN}✓ Paket ungoogled-chromium-bin berhasil di-uninstall.${NC}"
else
    echo -e "${CYAN}Paket ungoogled-chromium-bin sudah tidak terpasang di sistem.${NC}"
fi

echo -e "\n${GREEN}${BOLD}====================================================${NC}"
echo -e "${GREEN}${BOLD}✓ SUKSES: Data dibersihkan & Ungoogled Chromium telah di-uninstall!${NC}"
echo -e "${GREEN}${BOLD}====================================================${NC}\n"
