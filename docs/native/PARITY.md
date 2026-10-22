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
| 2. Daemon/services | belum | |
| 3. Panel inti + daily | belum | |
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

## Angka

| Komponen | PSS |
| --- | ---: |
| Quickshell baseline | 344–355 MB |
| Quickshell + bar | 139 MB |
| `dots-bar` 1 output | 11,3 MB |
| `dots-bar` 2 output (1366 + 1920) | 12,5 MB |
| `dotsd` idle | 0,4 MB |

## Gate berikutnya

G1 belum lulus karena:

- interaksi panel masih Quickshell;
- popup belum interaktif penuh;
- belum diuji 3 hari sebagai bar default.

Vertical bar dan corner style sudah selesai (hug/float/plain, kiri/kanan,
termasuk popup di samping bar). Langkah berikutnya: port panel inti ke Wayland
native; setelah itu baru `dots-session backend native` boleh dibuka.
