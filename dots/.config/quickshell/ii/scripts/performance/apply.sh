#!/usr/bin/env bash
# ==============================================================================
# Script: apply.sh
# Deskripsi: Menerapkan tuning performa (RAM / VRAM / CPU) yang dipilih di
#            halaman "Performance" pada aplikasi pengaturan illogical-impulse.
#
# Script ini WAJIB berjalan sebagai root. Dari UI, script dipanggil lewat
# `pkexec`, jadi dialog autentikasi polkit milik shell yang muncul.
#
# Pemakaian:
#   apply.sh memory   zram_percent=<persen> swappiness=<nilai>
#   apply.sh cpu      profile=<battery|balanced|performance> boost=<0|1> max_perf=<persen>
#   apply.sh graphics gpu_freq=<persen>
#   apply.sh boot            # dipakai systemd saat boot, baca /etc/ii-performance.conf
#
# Semua nilai disimpan di /etc/ii-performance.conf supaya tidak hilang setelah
# reboot. zram dikonfigurasi lewat /etc/systemd/zram-generator.conf dan
# swappiness lewat /etc/sysctl.d/99-ii-performance.conf (keduanya permanen).
#
# Output: baris "STATUS <kunci>=<nilai>" supaya UI bisa menampilkan hasil.
# ==============================================================================

set -uo pipefail

CONF_FILE="/etc/ii-performance.conf"
SYSCTL_FILE="/etc/sysctl.d/99-ii-performance.conf"
ZRAM_CONF="/etc/systemd/zram-generator.conf"
UNIT_FILE="/etc/systemd/system/ii-performance.service"
BOOT_SCRIPT="/usr/local/lib/ii-performance-apply"

# Data zram yang masih terpakai. Kalau RAM penuh, zram tidak boleh di-reset
# (swapoff akan memindahkan semua data kembali ke RAM dan bisa OOM).
ZRAM_LIVE_LIMIT_BYTES=$((256 * 1024 * 1024))

log_status() {
    echo "STATUS $1"
}

log_error() {
    echo "ERROR $1" >&2
}

require_root() {
    if [[ "${EUID}" -ne 0 ]]; then
        log_error "harus dijalankan sebagai root"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Penyimpanan konfigurasi
# ---------------------------------------------------------------------------

conf_get() {
    local key="$1"
    [[ -f "${CONF_FILE}" ]] || return 0
    sed -n "s/^${key}=//p" "${CONF_FILE}" | tail -n 1
}

conf_set() {
    local key="$1"
    local value="$2"
    mkdir -p "$(dirname "${CONF_FILE}")"
    touch "${CONF_FILE}"
    if grep -q "^${key}=" "${CONF_FILE}"; then
        sed -i "s|^${key}=.*|${key}=${value}|" "${CONF_FILE}"
    else
        echo "${key}=${value}" >>"${CONF_FILE}"
    fi
}

# ---------------------------------------------------------------------------
# RAM: swappiness + ukuran zram
# ---------------------------------------------------------------------------

apply_swappiness() {
    local value="$1"
    if ! [[ "${value}" =~ ^[0-9]+$ ]] || ((value < 0 || value > 200)); then
        log_error "swappiness tidak valid: ${value}"
        return 1
    fi

    mkdir -p "$(dirname "${SYSCTL_FILE}")"
    cat >"${SYSCTL_FILE}" <<EOF
# Diatur dari pengaturan illogical-impulse (halaman Performance).
# Nilai tinggi cocok untuk zram: halaman dingin dipindah ke RAM terkompresi.
vm.swappiness = ${value}
EOF

    sysctl -q -w vm.swappiness="${value}" >/dev/null 2>&1 || {
        log_error "gagal menulis vm.swappiness"
        return 1
    }

    conf_set SWAPPINESS "${value}"
    log_status "swappiness=applied"
}

zram_used_bytes() {
    local mm_stat="/sys/block/zram0/mm_stat"
    [[ -r "${mm_stat}" ]] || echo 0
    # Kolom pertama mm_stat adalah ukuran data asli (sebelum kompresi).
    awk '{print $1}' "${mm_stat}" 2>/dev/null || echo 0
}

apply_zram() {
    local percent="$1"
    if ! [[ "${percent}" =~ ^[0-9]+$ ]] || ((percent < 10 || percent > 100)); then
        log_error "ukuran zram tidak valid: ${percent}"
        return 1
    fi

    mkdir -p "$(dirname "${ZRAM_CONF}")"
    cat >"${ZRAM_CONF}" <<EOF
[zram0]
zram-size = ${percent}%
compression-algorithm = zstd
swap-priority = 100
fs-type = swap
EOF

    conf_set ZRAM_PERCENT "${percent}"

    # Kalau zram sedang dipakai berat, resized saat runtime bisa membuat
    # sistem kehabisan RAM. Simpan saja konfigurasi, berlaku setelah reboot.
    local used
    used="$(zram_used_bytes)"
    if [[ -n "${used}" ]] && ((used > ZRAM_LIVE_LIMIT_BYTES)); then
        log_status "zram=pending_reboot"
        return 0
    fi

    if systemctl restart systemd-zram-setup@zram0.service >/dev/null 2>&1; then
        log_status "zram=applied"
    else
        log_status "zram=pending_reboot"
    fi
}

# ---------------------------------------------------------------------------
# CPU: power profile, turbo boost, batas frekuensi
# ---------------------------------------------------------------------------

profile_settings() {
    case "$1" in
        battery)
            echo "powersave power"
            ;;
        performance)
            echo "performance performance"
            ;;
        *)
            echo "powersave balance_power"
            ;;
    esac
}

apply_governor() {
    local governor="$1"
    local found=0
    for file in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
        [[ -w "${file}" ]] || continue
        echo "${governor}" >"${file}" 2>/dev/null || continue
        found=1
    done
    [[ "${found}" -eq 1 ]]
}

apply_energy_preference() {
    local preference="$1"
    local found=0
    for file in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do
        [[ -w "${file}" ]] || continue
        echo "${preference}" >"${file}" 2>/dev/null || continue
        found=1
    done
    [[ "${found}" -eq 1 ]]
}

apply_boost() {
    local enabled="$1"
    local file="/sys/devices/system/cpu/intel_pstate/no_turbo"
    [[ -w "${file}" ]] || return 1
    # no_turbo = 1 berarti turbo boost dimatikan.
    if [[ "${enabled}" == "1" ]]; then
        echo 0 >"${file}"
    else
        echo 1 >"${file}"
    fi
}

apply_max_perf() {
    local percent="$1"
    local file="/sys/devices/system/cpu/intel_pstate/max_perf_pct"
    [[ -w "${file}" ]] || return 1
    echo "${percent}" >"${file}"
}

apply_cpu_settings() {
    local profile="$1"
    local boost="$2"
    local max_perf="$3"

    local settings
    settings="$(profile_settings "${profile}")"
    local governor="${settings%% *}"
    local preference="${settings##* }"

    if apply_governor "${governor}"; then
        log_status "governor=${governor}"
    else
        log_error "governor tidak bisa diubah (intel_pstate aktif?)"
    fi

    if apply_energy_preference "${preference}"; then
        log_status "energy_preference=${preference}"
    fi

    if apply_boost "${boost}"; then
        log_status "boost=${boost}"
    fi

    if apply_max_perf "${max_perf}"; then
        log_status "max_perf=${max_perf}"
    fi

    conf_set CPU_PROFILE "${profile}"
    conf_set CPU_BOOST "${boost}"
    conf_set CPU_MAX_PERF "${max_perf}"
}

# ---------------------------------------------------------------------------
# Grafis: batas frekuensi iGPU
# ---------------------------------------------------------------------------

gpu_frequency_files() {
    local file
    for file in /sys/class/drm/card*/gt_max_freq_mhz; do
        [[ -w "${file}" ]] && echo "${file}"
    done
}

gpu_hardware_max() {
    local file="$1"
    # gt_RP0_freq_mhz adalah frekuensi maksimum hardware (tidak ikut berubah
    # kalau gt_max_freq_mhz diturunkan).
    local base="${file%/gt_max_freq_mhz}"
    local rp0="${base}/gt_RP0_freq_mhz"
    if [[ -r "${rp0}" ]]; then
        cat "${rp0}"
    else
        cat "${file}"
    fi
}

apply_gpu_frequency() {
    local percent="$1"
    if ! [[ "${percent}" =~ ^[0-9]+$ ]] || ((percent < 10 || percent > 100)); then
        log_error "batas frekuensi GPU tidak valid: ${percent}"
        return 1
    fi

    local file
    local applied=0
    for file in $(gpu_frequency_files); do
        local max
        max="$(gpu_hardware_max "${file}")"
        [[ -n "${max}" ]] || continue
        local target=$((max * percent / 100))
        ((target < 1)) && target=1
        echo "${target}" >"${file}" 2>/dev/null || continue
        applied=1
        log_status "gpu_freq=${target}"
    done

    if [[ "${applied}" -eq 0 ]]; then
        log_error "batas frekuensi GPU tidak didukung di mesin ini"
        return 1
    fi

    conf_set GPU_FREQ_PERCENT "${percent}"
}

# ---------------------------------------------------------------------------
# Unit systemd supaya pengaturan CPU/GPU tetap berlaku setelah reboot
# ---------------------------------------------------------------------------

install_boot_unit() {
    mkdir -p "$(dirname "${BOOT_SCRIPT}")" "$(dirname "${UNIT_FILE}")"
    cp -f "$0" "${BOOT_SCRIPT}"
    chmod 0755 "${BOOT_SCRIPT}"

    cat >"${UNIT_FILE}" <<EOF
[Unit]
Description=Apply CPU and GPU tuning set in illogical-impulse settings
After=multi-user.target

[Service]
Type=oneshot
ExecStart=${BOOT_SCRIPT} boot
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload >/dev/null 2>&1
    systemctl enable ii-performance.service >/dev/null 2>&1
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

apply_boot() {
    local profile governor preference boost max_perf gpu_freq
    profile="$(conf_get CPU_PROFILE)"
    boost="$(conf_get CPU_BOOST)"
    max_perf="$(conf_get CPU_MAX_PERF)"
    gpu_freq="$(conf_get GPU_FREQ_PERCENT)"

    [[ -n "${profile}" ]] && apply_cpu_settings "${profile}" "${boost:-1}" "${max_perf:-100}"
    [[ -n "${gpu_freq}" ]] && apply_gpu_frequency "${gpu_freq}"
    return 0
}

main() {
    require_root

    local action="${1:-}"
    shift || true

    case "${action}" in
        boot)
            apply_boot
            ;;
        memory)
            for arg in "$@"; do
                case "${arg}" in
                    zram_percent=*) apply_zram "${arg#*=}" ;;
                    swappiness=*) apply_swappiness "${arg#*=}" ;;
                    *) log_error "argumen tidak dikenal: ${arg}" ;;
                esac
            done
            install_boot_unit
            ;;
        cpu)
            local profile="" boost="" max_perf=""
            for arg in "$@"; do
                case "${arg}" in
                    profile=*) profile="${arg#*=}" ;;
                    boost=*) boost="${arg#*=}" ;;
                    max_perf=*) max_perf="${arg#*=}" ;;
                    *) log_error "argumen tidak dikenal: ${arg}" ;;
                esac
            done
            apply_cpu_settings "${profile:-balanced}" "${boost:-1}" "${max_perf:-100}"
            install_boot_unit
            ;;
        graphics)
            for arg in "$@"; do
                case "${arg}" in
                    gpu_freq=*) apply_gpu_frequency "${arg#*=}" ;;
                    *) log_error "argumen tidak dikenal: ${arg}" ;;
                esac
            done
            install_boot_unit
            ;;
        *)
            log_error "aksi tidak dikenal: ${action}"
            exit 1
            ;;
    esac
}

main "$@"
