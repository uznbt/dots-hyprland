# Parity Native Shell

Dokumen ini adalah kontrak hidup untuk migrasi Quickshell ke C. Status di sini
harus diperbarui setiap fase.

Baseline: branch `native/c-shell`, tag `legacy/quickshell-2026-10`.

## Aturan

- Quickshell tetap menjadi fallback sampai gate native daily driver lolos.
- `Persistent.qml` dan `TimerIndicator.qml` adalah file user; jangan diubah.
- Perubahan perilaku harus dicatat di sini sebelum dianggap selesai.
- Ukur PSS/RSS/anon dengan `/proc/<pid>/smaps_rollup`, bukan perkiraan.

## Status fase

| Fase | Status | Catatan |
| --- | --- | --- |
| 0. Baseline/kontrak | selesai | branch, tag, backup, matrix awal |
| 1. Fondasi + bar | berjalan | `dots-bar`, `dotsd`, `dots-ctl`, `dots-dispatch`, `dots-session` |
| 2. Daemon/services | belum | fondasi IPC sudah ada, layanan sistem menyusul |
| 3. Panel inti + daily | berjalan | OSD volume/brightness native; notifikasi/media/dock menyusul |
| 4. Sidebar/overview | belum | |
| 5. Settings/utilitas | belum | |
| 6. Decommission | belum | |

## Bar

| Fitur | Native | Catatan |
| --- | --- | --- |
| Layout responsif horizontal | ya | grup tengah/popup dihitung dari lebar output |
| Workspace + app icon | ya | data Hyprland IPC |
| Window aktif | ya | |
| Media | ya | `playerctl` |
| RAM/swap/CPU | ya | config Quickshell dibaca |
| Jam/tanggal/uptime | ya | |
| Baterai/UPower | ya | |
| Bell notifikasi | ya | butuh bridge state Quickshell |
| Todo popup | ya | `todo.json` |
| Popup jam/resource/baterai | ya | non-interaktif, seperti QML |
| Warna Material You | sebagian | background/primary/on-*/secondary; layer1 masih konstanta |
| Klik sidebar | ya | lewat `dotsd` -> adapter Quickshell |
| Scroll brightness/volume/workspace | ya | |
| Auto-hide | ya | hover strip + pushWindows |
| Bar bottom | ya | |
| Shortened form | ya | ambang 1200/1000 px seperti BarContent.qml |
| Vertical bar | ya | kiri/kanan; konten, workspace, jam, baterai, indikator bawah |
| Corner style | ya | 0 hug, 1 float (margin 5, radius 18, border), 2 plain |
| `showBackground` / `borderless` | ya | background dan kartu grup mengikuti config |
| Tray | belum | |
| Multi-monitor | ya | satu proses, surface per output |
| `screenList` | ya | filter output dari config |
| Hotplug output | ya | uji create/remove headless output |
| Supervisor hybrid | ya | `dots-session start/stop` |
| Backend switch | sebagian | `hybrid`/`quickshell`; `native` ditolak sampai panel native ada |

## OSD

| Fitur | Native | Catatan |
| --- | --- | --- |
| Volume naik/turun/mute | ya | `wpctl`; nilai dibaca ulang setelah aksi |
| Brightness naik/turun | sebagian | `brightnessctl` untuk output internal; monitor eksternal tetap Quickshell/ddcutil |
| Kartu OSD | ya | 200×64 px, ikon Material Symbols, label, persen, progress |
| Posisi atas/bawah | ya | mengikuti `bar.bottom`; margin `barHeight` (40/50) |
| Monitor fokus | ya | `hyprctl monitors -j`; fallback output pertama; `DOTS_OUTPUT` untuk tes |
| Timeout | ya | `osd.timeout` (default 1000 ms), override `DOTS_OSD_TIMEOUT_MS` |
| Fade in/out | ya | 120/140 ms |
| Singleton tanpa proses idle | ya | Unix datagram `$XDG_RUNTIME_DIR/dots/osd.sock`; proses keluar setelah fade |
| Trigger keybind | ya | `dots-dispatch volume`/brightness internal bila ada di PATH; fallback `wpctl`/Quickshell |
| Bridge OSD Quickshell | ya | `osdVolume nativeOn/nativeOff` + safety timer; fallback QML tetap hidup |
| Rotasi/scale ikon brightness | belum | |
| ddcutil / monitor eksternal | belum | baru backlight internal |
| Lokalisasi label | belum | label Inggris |
| Warna Material You penuh | sebagian | memakai konstanta layer/primary yang sama dengan bar |

## Angka

| Komponen | PSS |
| --- | ---: |
| Quickshell baseline | 344–355 MB |
| Quickshell + bar | 139 MB |
| `dots-bar` 1 output | 11,3 MB |
| `dots-bar` 2 output (1366 + 1920) | 12,5 MB |
| `dots-osd` saat tampil | 9,3 MB (RSS 35,9 MB, anon 4,6 MB) |
| `dots-osd` idle | 0 MB (proses keluar) |
| `dotsd` idle | 0,4 MB |

## Gate berikutnya

G1 belum lulus karena:

- interaksi panel masih Quickshell;
- popup belum interaktif penuh;
- belum diuji 3 hari sebagai bar default.

Vertical bar dan corner style sudah selesai (hug/float/plain, kiri/kanan,
termasuk popup di samping bar). OSD volume/brightness native juga sudah
berjalan, termasuk bridge supaya OSD Quickshell tidak tampil ganda saat keybind
memakai `dots-dispatch` atau saat `dots-osd` dipanggil langsung.

Langkah berikutnya: notification server/popup native (jangan ambil alih
`org.freedesktop.Notifications` sebelum popup siap), lalu media controls, dock,
session/power menu, display panel, wallpaper, dan lock. Setelah panel inti itu
ada, baru `dots-session backend native` boleh dibuka.
