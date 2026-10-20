#!/usr/bin/env bash
# ==============================================================================
# Script: clean_sober.sh
# Deskripsi: Menghapus seluruh data, cache, config, cookie, state, dan sisa
#            instalasi Sober (Roblox on Linux) tanpa terkecuali.
# ==============================================================================

set -eo pipefail

# Warna output terminal
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

APP_ID="org.vinegarhq.Sober"

# Parse argumen terlebih dahulu
AUTO_CONFIRM=false
UNINSTALL_APP=false

for arg in "$@"; do
    case "$arg" in
        -y|--yes)
            AUTO_CONFIRM=true
            ;;
        -u|--uninstall)
            UNINSTALL_APP=true
            ;;
        -h|--help)
            echo "Penggunaan: $0 [OPSI]"
            echo ""
            echo "Opsi:"
            echo "  -y, --yes        Hapus langsung tanpa konfirmasi interaktif"
            echo "  -u, --uninstall  Hapus juga aplikasi Flatpak Sober dari sistem"
            echo "  -h, --help       Tampilkan panduan bantuan ini"
            exit 0
            ;;
        *)
            echo -e "${RED}Opsi tidak dikenal: $arg${NC}"
            echo "Gunakan -h atau --help untuk melihat panduan."
            exit 1
            ;;
    esac
done

echo -e "${CYAN}${BOLD}====================================================${NC}"
echo -e "${CYAN}${BOLD}       PEMBERSIH TOTAL DATA SOBER (ROBLOX LINUX)    ${NC}"
echo -e "${CYAN}${BOLD}====================================================${NC}"

# 1. Hentikan semua proses Sober yang sedang berjalan
echo -e "\n${BLUE}[1/5] Menghentikan semua proses Sober yang sedang aktif...${NC}"
if command -v flatpak &>/dev/null; then
    flatpak kill "$APP_ID" 2>/dev/null || true
fi

# Cari dan hentikan proses Sober tanpa mematikan script ini sendiri ($$ / $PPID)
CURRENT_PID=$$
for pid in $(pgrep -i "sober" 2>/dev/null || true); do
    if [ "$pid" != "$CURRENT_PID" ] && [ "$pid" != "$PPID" ]; then
        cmdline=$(tr '\0' ' ' < /proc/"$pid"/cmdline 2>/dev/null || true)
        if [[ "$cmdline" != *"clean_sober"* ]]; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    fi
done
echo -e "${GREEN}✓ Semua proses Sober telah dihentikan.${NC}"

# 2. Daftar target folder & file data Sober
TARGET_PATHS=(
    "$HOME/.var/app/$APP_ID"
    "$HOME/.local/share/flatpak/overrides/$APP_ID"
    "$HOME/.config/sober"
    "$HOME/.cache/sober"
    "$HOME/.local/share/sober"
    "$HOME/.local/state/sober"
)

# 3. Pindai dan tampilkan file yang ditemukan
echo -e "\n${BLUE}[2/5] Memindai data Sober di sistem...${NC}"
FOUND_ANY=false

for target in "${TARGET_PATHS[@]}"; do
    if [ -e "$target" ]; then
        FOUND_ANY=true
        SIZE=$(du -sh "$target" 2>/dev/null | cut -f1)
        echo -e "  - ${YELLOW}Ditemukan:${NC} $target (${BOLD}$SIZE${NC})"
    fi
done

# Cari file sementara di /tmp dan /dev/shm
TMP_FILES=$(find /tmp /dev/shm -maxdepth 2 \( -iname "*sober*" -o -iname "*org.vinegarhq.Sober*" \) -user "$USER" 2>/dev/null || true)
if [ -n "$TMP_FILES" ]; then
    while IFS= read -r f; do
        if [ -n "$f" ]; then
            FOUND_ANY=true
            echo -e "  - ${YELLOW}File temp:${NC} $f"
        fi
    done <<< "$TMP_FILES"
fi

if [ "$FOUND_ANY" = false ]; then
    echo -e "${YELLOW}Tidak ada data Sober yang ditemukan di lokasi standar.${NC}"
fi

# 4. Minta konfirmasi jika tidak ada flag -y
if [ "$AUTO_CONFIRM" = false ]; then
    echo -e "\n${RED}${BOLD}PERINGATAN:${NC} Tindakan ini akan menghapus ${BOLD}SEMUA${NC} data Sober"
    echo -e "(termasuk login, cookies, roblox appData, cache shader, dan konfigurasi) secara ${BOLD}PERMANEN${NC}!"
    read -p "Apakah Anda yakin ingin menghapus seluruh data tersebut? (y/N): " -r CONFIRM
    if [[ ! "$CONFIRM" =~ ^[yY]([eE][sS])?$ ]]; then
        echo -e "${YELLOW}Operasi dibatalkan oleh pengguna.${NC}"
        exit 0
    fi
fi

# 5. Eksekusi penghapusan data
echo -e "\n${BLUE}[3/5] Menghapus seluruh data Sober...${NC}"
for target in "${TARGET_PATHS[@]}"; do
    if [ -e "$target" ]; then
        rm -rf "$target"
        echo -e "  ${GREEN}✓ Berhasil dihapus:${NC} $target"
    fi
done

# Hapus file temp
if [ -n "$TMP_FILES" ]; then
    while IFS= read -r f; do
        if [ -n "$f" ] && [ -e "$f" ]; then
            rm -rf "$f"
            echo -e "  ${GREEN}✓ Berhasil dihapus:${NC} $f"
        fi
    done <<< "$TMP_FILES"
fi

# 6. Uninstall paket aplikasi Flatpak jika diminta
echo -e "\n${BLUE}[4/5] Memeriksa paket aplikasi Flatpak...${NC}"
if [ "$UNINSTALL_APP" = true ]; then
    if flatpak list | grep -q "$APP_ID"; then
        echo -e "Menghapus aplikasi Flatpak $APP_ID..."
        flatpak uninstall --delete-data -y "$APP_ID" 2>/dev/null || true
        echo -e "${GREEN}✓ Aplikasi Flatpak Sober telah di-uninstall.${NC}"
    else
        echo -e "${YELLOW}Aplikasi Flatpak Sober tidak terpasang.${NC}"
    fi
else
    echo -e "${CYAN}Info: Paket aplikasi Sober tetap terpasang dalam keadaan fresh install (bersih).${NC}"
    echo -e "      (Gunakan opsi ${BOLD}-u${NC} atau ${BOLD}--uninstall${NC} jika ingin menghapus aplikasinya juga)."
fi

# 7. Verifikasi akhir
echo -e "\n${BLUE}[5/5] Melakukan verifikasi pembersihan...${NC}"
REMAINING=false
for target in "${TARGET_PATHS[@]}"; do
    if [ -e "$target" ]; then
        echo -e "  ${RED}✗ Masih tersisa:${NC} $target"
        REMAINING=true
    fi
done

if [ "$REMAINING" = false ]; then
    echo -e "\n${GREEN}${BOLD}====================================================${NC}"
    echo -e "${GREEN}${BOLD}✓ SUKSES: Seluruh data Sober berhasil dibersihkan!  ${NC}"
    echo -e "${GREEN}${BOLD}====================================================${NC}\n"
else
    echo -e "\n${RED}${BOLD}Perhatian: Beberapa file mungkin membutuhkan izin root untuk dihapus.${NC}\n"
fi
