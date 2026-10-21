# 📉 Catatan Optimasi RAM

Mesin ini punya **8 GB RAM (7,5 GiB usable)** + **zram 7,5 GB (zstd)**.
Dokumen ini menjelaskan hasil audit RAM, apa yang sudah diperbaiki, dan
lever yang masih bisa ditarik.

---

## 1. Hasil audit

Snapshot saat audit (sebelum perbaikan):

| Proses | RAM | Keterangan |
| --- | ---: | --- |
| Brave (Chromium) | 2.729 MB | 23 proses, 15 renderer |
| Spotify (Electron) | 1.078 MB | |
| WhatsZan (Electron) | 894 MB | |
| **Quickshell** | **730 MB** | **2 instance berjalan bersamaan** |
| **nmcli monitor** | **~500 MB** | **55 proses yatim** |
| opencode | 650 MB | sesi agent, sementara |
| zram terpakai | 2,7 GB | indikasi tekanan memori |

Total yang terbuang sia-sia: **~1,2 GB** tanpa mengurangi satu pun fitur.

---

## 2. Yang sudah diperbaiki

### 2.1 Proses `nmcli monitor` yatim — hemat ~500 MB

`services/Network.qml` menjalankan `nmcli monitor` sebagai proses anak yang
berjalan terus-menerus. **Quickshell tidak pernah mematikan proses anaknya** —
bukan saat `SIGTERM`, bukan saat `SIGKILL`. Akibatnya setiap reload, restart,
atau pembukaan aplikasi settings meninggalkan satu proses yatim.

Ditemukan **55 proses** yatim di mesin ini (masing-masing ~9 MB).

**Perbaikan:** `scripts/network/monitor.sh` menjalankan monitor di bawah sebuah
*watchdog* terpisah yang memantau dua hal:

* PID parent (Quickshell) → kalau Quickshell mati, monitor dibersihkan.
* PID script itu sendiri → kalau script-nya dibunuh, monitor dibersihkan.

Jadi monitor tidak pernah bisa tertinggal, berapa kali pun shell direstart.

### 2.2 Aplikasi settings sebagai proses kedua — hemat ~400 MB

`SUPER + I` dulu menjalankan `qs -p settings.qml`, yaitu **proses Quickshell
kedua** yang memuat ulang seluruh service dan widget (~400 MB ekstra di atas
shell yang sudah jalan).

**Perbaikan:** jendela settings sekarang di-host di dalam shell itu sendiri
dan dibuka lewat IPC:

```sh
qs -c ii ipc call settings open
```

Jendelanya di-*unload* lagi setelah ditutup, sehingga memorinya dikembalikan.
Proses `qs -p settings.qml` hanya dipakai sebagai cadangan kalau shell sama
sekali tidak berjalan.

> **Catatan quirk:** Quickshell **tidak** menjalankan fungsi IPC yang namanya
> persis `show`. Dari sinilah asal IPC target `settings` memakai fungsi
> `open` / `close`.

### 2.3 Tuning ringan config shell

| Opsi | Nilai | Alasan |
| --- | --- | --- |
| `sidebar.keepRightSidebarLoaded` | `false` | Sidebar kanan di-unload saat ditutup |
| `resources.updateInterval` | `5000` | Poll CPU/RAM tiap 5 detik, bukan 3 |

---

## 3. Lever yang belum ditarik

### 3.1 Batasi renderer Chromium/Electron — potensi ~1–1,5 GB

Tiga aplikasi Chromium/Electron ini total memakan ~4,7 GB. Semuanya membuat satu
proses renderer per tab (~150 MB masing-masing).

Solusinya: `scripts/electron-lowram.sh` membungkus binary dengan flag:

```
--renderer-process-limit=8      # tab lebih dari 8 berbagi proses
--js-flags=--max-old-space-size=512
--disk-cache-size=134217728
```

Override `.desktop` level-user (tanpa sudo) sudah terpasang untuk Brave,
Spotify, dan WhatsZan:

| File | Lokasi |
| --- | --- |
| Override aplikasi | `~/.local/share/applications/*.desktop` |
| Override autostart | `~/.config/autostart/WhatsZan.desktop` |
| Sumber | `dots-extra/desktop-overrides/` |
| Pasang ulang | `dots-extra/desktop-overrides/install.sh` |

> ⚠️ **Flag hanya berlaku untuk instance yang baru start.** Tutup penuh
> (bukan hanya jendela) Brave / Spotify / WhatsZan agar flag-nya dipakai.
> Perhatikan juga: Brave tidak memulihkan tab secara default
> (`restore_on_startup` tidak disetel), jadi simpan tab dulu sebelum ditutup.

Tuning lewat env var:

```sh
LOWRAM_RENDERER_LIMIT=6   # makin kecil = makin hemat, makin lambat
LOWRAM_JS_HEAP_MB=256
```

### 3.2 Matikan blur Hyprland — hemat GPU, bukan RAM

`general.lua` memakai `blur` dengan `xray = true` dan `passes = 3`. Ini
membebani GPU/iGPU dan membuat compositor lambat, terutama saat dipakai
bersamaan dengan tiga aplikasi berat di atas. Tidak diubah karena ini
preferensi visual — ubah sendiri kalau mau lebih ringan:

```lua
blur = { enabled = false }
```

### 3.3 Audio & lokasi

* `easyeffects` dan `geoclue` sebenarnya **tidak jalan** di mesin ini (binary
  tidak ditemukan), jadi tidak ada yang perlu dimatikan.
* `hypridle` dan `gnome-keyring-daemon` sebaiknya tetap jalan: `hypridle`
  untuk lock screen, `gnome-keyring` untuk unlock keyring.

### 3.4 Zram & swappiness

Sudah pas: `zram-size = ram` dengan algoritma zstd, `vm.swappiness = 150`.
Angka swappiness yang tinggi memang benar untuk zram — halaman dingin
dipindah ke RAM terkompresi, bukan ke disk. **Tidak diubah.**

Kalau masih sering swapping, turunkan `zram-size` ke `50%` (atau `max` 4G)
supaya RAM tidak terlalu banyak dipakai untuk data terkompresi.

---

## 4. Cara memverifikasi

```sh
# Ringkasan per aplikasi
ps -eo rss,comm --no-headers \
  | awk '{r[$2]+=$1} END {for (i in r) printf "%8.0f MB  %s\n", r[i]/1024, i}' \
  | sort -rn | head -15

# Pastikan tidak ada proses yatim
ps -eo pid,ppid,args --no-headers | grep "nmcli monito[r]"

# Pastikan hanya ada satu instance Quickshell
pgrep -ax qs
```

Hasil yang diharapkan:

* `nmcli monitor` → **1** proses, dengan parent `monitor.sh`.
* `qs` → **1** proses.
* `free -h` → `available` naik dibanding sebelum perbaikan.
