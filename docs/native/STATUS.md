# Status Native Shell

Checkpoint hidup. Perbarui di akhir setiap sesi. Kontrak fitur ada di
[`PARITY.md`](PARITY.md); rencana besar ada di
`/home/uznbt/.opencode/plan/rombak-dots-ke-c.md`.

Terakhir diperbarui: **23 Oktober 2026**

## Posisi repo

- Branch: `native/c-shell`
- Commit terakhir: jalankan `git log -1 --oneline`.
- Baseline Quickshell: tag `legacy/quickshell-2026-10` (branch `main`).
- Perubahan user yang belum di-commit dan **jangan disentuh**:
  - `dots/.config/quickshell/ii/modules/common/Persistent.qml`
  - `dots/.config/quickshell/ii/modules/ii/timerIndicator/TimerIndicator.qml`

## Sudah jalan

- `dots-bar`: bar Wayland C, satu proses satu surface per output.
- Multi-monitor, `screenList`, hotplug output.
- Bar atas/bawah, auto-hide + hover strip + `pushWindows`.
- Bentuk shortened horizontal: ambang 1200 px dan 1000 px, mengikuti
  `BarContent.qml`.
- Bar vertical kiri/kanan (`bar.vertical`, `bar.bottom`), termasuk layout
  resources/media, workspace, jam, baterai, dan indikator kanan bawah.
- Corner style `0` hug (dekorasi sudut layar), `1` float (margin 5 px,
  radius 18, border), `2` plain; `showBackground` dan `borderless` dihormati.
- Perubahan `vertical`, `bottom`, atau `cornerStyle` saat runtime membuat ulang
  layer surface tanpa restart proses.
- Workspace/app icon, window aktif, media (`playerctl`), RAM/swap/CPU,
  jam/tanggal, baterai/UPower, bell notifikasi.
- Popup jam/resource/baterai (non-interaktif, sama seperti QML).
- Klik sidebar dan scroll brightness/volume/workspace lewat `dotsd` → Quickshell.
- `dotsd`, `dots-ctl`, `dots-dispatch`, `dots-session` (mode hybrid).
- Smoke test IPC lulus.
- PSS bar 1 output: ~11,3 MB; 2 output: ~12,5 MB. `dotsd` idle: ~0,4 MB.

## Sedang dikerjakan / berikutnya

Vertical bar dan corner style sudah selesai dan diuji visual pada 1366×768
(horizontal/vertical, kiri/kanan, hug/float) serta headless 1920×1080 untuk
multi-output. Berikutnya port panel inti native (sidebar, overview, OSD, dock)
agar `dots-session backend native` bisa dibuka.

## Belum ada

- Tray StatusNotifierItem.
- Popup interaktif penuh dan animasi auto-hide.
- Panel sidebar/overview/OSD/dock native; masih Quickshell.
- `dots-session backend native` (sengaja ditolak sampai panel native ada).
- Uji 3 hari sebagai bar default.

## Verifikasi minimal sebelum commit

```sh
meson compile -C native/build
meson test -C native/build --print-errorlogs
```

Untuk perubahan visual, lampirkan hasil `grim` dan ukuran PSS di ringkasan
commit/PR. Jika mengubah config live untuk tes, kembalikan dari backup.
