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
| 1. Fondasi + bar | berjalan | `dots-bar` build dan berjalan |
| 2. Daemon/services | belum | |
| 3. Panel inti + daily | belum | |
| 4. Sidebar/overview | belum | |
| 5. Settings/utilitas | belum | |
| 6. Decommission | belum | |

## Bar

| Fitur | Native | Catatan |
| --- | --- | --- |
| Geometri 1366×768 | ya | masih konstanta prototipe |
| Workspace + app icon | ya | data Hyprland IPC |
| Window aktif | ya | |
| Media | ya | `playerctl` |
| RAM/swap/CPU | ya | config Quickshell dibaca |
| Jam/tanggal/uptime | ya | |
| Baterai/UPower | ya | |
| Bell notifikasi | ya | butuh bridge state Quickshell |
| Todo popup | ya | `todo.json` |
| Popup jam/resource/baterai | ya | non-interaktif, seperti QML |
| Klik sidebar | ya | masih lewat IPC Quickshell |
| Scroll brightness/volume/workspace | ya | |
| Auto-hide | belum | |
| Bar bottom | belum | |
| Shortened form | belum | |
| Tray | belum | |
| Multi-monitor | belum | |
| Layout non-1366×768 | belum | target Fase 1 lanjutan |

## Angka

| Komponen | PSS |
| --- | ---: |
| Quickshell baseline | 344–355 MB |
| Quickshell + bar | 139 MB |
| `dots-bar` | 10,9 MB |

## Gate berikutnya

G1 belum lulus karena:

- geometri masih hardcoded;
- belum multi-monitor;
- belum auto-hide/bottom/shortened;
- interaksi panel masih Quickshell;
- belum diuji 3 hari sebagai bar default.

Langkah berikutnya: refactor layout bar, lalu `dotsd` + `dots-ctl`.
