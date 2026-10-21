# ⚙️ Halaman "Performance" (RAM / VRAM / CPU)

Semua tuning performa mesin sekarang bisa diatur dari **aplikasi pengaturan
shell** (`SUPER + I` → tab **Performance**), bukan hanya lewat edit file
manual.

| Bagian | Yang bisa diatur | Efek |
| --- | --- | --- |
| **Memory (RAM)** | Renderer processes | `--renderer-process-limit` untuk Brave/Spotify/WhatsZan |
| | JavaScript heap per renderer (MB) | `--js-flags=--max-old-space-size` |
| | Disk cache per app (MB) | `--disk-cache-size` |
| | zram size (% of RAM) | `/etc/systemd/zram-generator.conf` |
| | Swappiness | `/etc/sysctl.d/99-ii-performance.conf` |
| **Graphics (VRAM)** | Blur | `hyprctl keyword decoration:blur:enabled` (langsung berlaku) |
| | Maximum frequency (%) | batas frekuensi iGPU (`gt_max_freq_mhz`) |
| **CPU** | Power profile | governor + energy performance preference |
| | Turbo boost | `intel_pstate/no_turbo` |
| | Maximum frequency (%) | `intel_pstate/max_perf_pct` |

> Pada mesin ini VRAM = RAM yang dipinjam iGPU Intel UHD 620, jadi "mengatur
> VRAM" lebih ke arah mengurangi beban GPU (blur, frekuensi) daripada
> memesan VRAM khusus.

---

## 1. Cara kerja

* Nilai UI disimpan di `~/.config/illogical-impulse/config.json` pada bagian
  `performance`.
* Pengubahan yang butuh hak root memanggil
  `dots/.config/quickshell/ii/scripts/performance/apply.sh` lewat `pkexec`,
  jadi **dialog password administrator milik shell** yang muncul (polkit
  agent bawaan ii).
* Nilai final disimpan lagi di `/etc/ii-performance.conf` dan dipasang
  permanent lewat:
  * `/etc/systemd/zram-generator.conf` (zram)
  * `/etc/sysctl.d/99-ii-performance.conf` (swappiness)
  * service `ii-performance.service` (CPU + GPU, dijalankan lagi setelah boot)
* Nilai untuk aplikasi Chromium/Electron dibaca wrapper
  `dots/.config/hypr/hyprland/scripts/electron-lowram.sh` **saat aplikasi
  start**, jadi aplikasi harus ditutup penuh supaya nilai baru dipakai.
  Variabel lingkungan (`LOWRAM_RENDERER_LIMIT`, `LOWRAM_JS_HEAP_MB`,
  `LOWRAM_DISK_CACHE_MB`) tetap menang kalau diisi.

## 2. Catatan penting

* **zram tidak langsung di-reset kalau RAM sedang penuh.** `swapoff` akan
  memindahkan semua data zram kembali ke RAM dan bisa memicu OOM. Kalau
  zram masih dipakai berat, ukuran baru disimpan dan baru berlaku setelah
  reboot (UI menampilkan pemberitahuan).
* **Blur** berlaku langsung di compositor, tapi kalau Hyprland di-reload
  nilainya kembali ke `general.lua`. Nilai di config dipakai lagi saat
  halaman Performance dibuka.
* Setting CPU/GPU di `sysfs` hilang setelah reboot, thats kenapa ada
  `ii-performance.service`.

## 3. Verifikasi manual

```sh
# nilai yang tersimpan di sistem
cat /etc/ii-performance.conf
cat /etc/systemd/zram-generator.conf
cat /etc/sysctl.d/99-ii-performance.conf

# status cpu
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference
cat /sys/devices/system/cpu/intel_pstate/no_turbo
cat /sys/devices/system/cpu/intel_pstate/max_perf_pct

# batas frekuensi gpu
cat /sys/class/drm/card*/gt_max_freq_mhz

# service boot
systemctl status ii-performance.service
```
