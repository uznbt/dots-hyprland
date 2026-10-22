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
| Shortened form | belum | |
| Tray | belum | |
| Multi-monitor | belum | |
| Shortened/vertical | belum | |
| Supervisor hybrid | ya | `dots-session start/stop` |
| Backend switch | sebagian | `hybrid`/`quickshell`; `native` ditolak sampai panel native ada |

## Angka

| Komponen | PSS |
| --- | ---: |
| Quickshell baseline | 344–355 MB |
| Quickshell + bar | 139 MB |
| `dots-bar` | 11,0 MB |
| `dotsd` idle | 0,4 MB |

## Gate berikutnya

G1 belum lulus karena:

- belum multi-monitor;
- belum shortened/vertical;
- interaksi panel masih Quickshell;
- popup belum interaktif penuh;
- belum diuji 3 hari sebagai bar default.

Langkah berikutnya: multi-output + surface per monitor, lalu port panel inti
ke Wayland native; setelah itu baru `dots-session backend native` boleh dibuka.
