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
- `dots-osd`: overlay volume/brightness native (layer overlay, kartu 200×64,
  fade in/out, timeout dari `osd.timeout`). Proses hanya hidup selama OSD
  tampil; invocation berikutnya mengirim command lewat Unix datagram socket.
- `dots-dispatch volume up|down|mute` memakai `dots-osd` bila binary tersedia.
  `brightness up|down` memakai OSD native hanya saat output fokus internal
  (`eDP`/`LVDS`/`DSI`); monitor eksternal tetap Quickshell/ddcutil. Keybind repo
  memakai jalur ini dengan fallback `wpctl`/Quickshell, jadi keybind tetap
  hidup tanpa binary native.
- Bridge transisi: `dots-osd` memanggil `qs -c ii ipc call osdVolume
  nativeOn/nativeOff` supaya OSD Quickshell tidak tampil ganda. Timer pengaman
  di Quickshell mengaktifkan kembali OSD QML kalau proses native mati.
- Smoke test IPC dan OSD lulus.
- PSS bar 1 output: ~11,3 MB; 2 output: ~12,5 MB. `dotsd` idle: ~0,4 MB.
  `dots-osd` saat tampil: PSS ~9,3 MB, RSS ~35,9 MB; idle: 0 proses.

## Sedang dikerjakan / berikutnya

Bar horizontal/vertical dan corner style sudah selesai dan diuji visual pada
1366×768 (horizontal/vertical, kiri/kanan, hug/float) serta headless 1920×1080
untuk multi-output. OSD volume/brightness native sudah menjadi panel inti
pertama dan teruji visual di atas/bawah bar serta pada output headless.

Berikutnya sesuai urutan Fase 3: notification server/popup native, media
controls, dock, session/power menu, display panel, wallpaper, lalu lock. Jangan
ambil alih `org.freedesktop.Notifications` sebelum popup native siap.

## Belum ada

- Tray StatusNotifierItem.
- Popup interaktif penuh dan animasi auto-hide.
- Notification server/popup, media controls, dock, session/power, display
  panel, wallpaper, dan lock native.
- Sidebar/overview native; masih Quickshell.
- OSD baru mendukung volume/brightness internal (`wpctl`/`brightnessctl`);
  ddcutil, rotasi/scale ikon, dan lokalisasi label belum.
- `dots-session backend native` (sengaja ditolak sampai panel native ada).
- Uji 3 hari sebagai bar default.

## Verifikasi minimal sebelum commit

```sh
meson compile -C native/build
meson test -C native/build --print-errorlogs
```

Untuk perubahan visual, lampirkan hasil `grim` dan ukuran PSS di ringkasan
commit/PR. Jika mengubah config live untuk tes, kembalikan dari backup.
