# Status Native Shell

Checkpoint hidup. Perbarui di akhir setiap sesi. Kontrak fitur ada di
[`PARITY.md`](PARITY.md); rencana besar ada di
`/home/uznbt/.opencode/plan/rombak-dots-ke-c.md`.

Terakhir diperbarui: **23 Oktober 2026**

## Posisi repo

- Branch: `native/c-shell`
- Commit terakhir: `0e2b3ed feat(native): add responsive shortened bar forms`
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
- Workspace/app icon, window aktif, media (`playerctl`), RAM/swap/CPU,
  jam/tanggal, baterai/UPower, bell notifikasi.
- Popup jam/resource/baterai (non-interaktif, sama seperti QML).
- Klik sidebar dan scroll brightness/volume/workspace lewat `dotsd` → Quickshell.
- `dotsd`, `dots-ctl`, `dots-dispatch`, `dots-session` (mode hybrid).
- Smoke test IPC lulus.
- PSS bar 1 output: ~11 MB; 2 output: ~12,9 MB. `dotsd` idle: ~0,4 MB.

## Sedang dikerjakan / berikutnya

Prioritas gate G1: **vertical bar + corner style**, lalu panel inti native.

1. Vertical bar (`bar.vertical = true`), kiri/kanan mengikuti `bar.bottom`.
2. Corner style:
   - `0` Hug: background penuh + dekorasi sudut layar.
   - `1` Float: margin 5 px, radius 18, border.
   - `2` Plain: background persegi penuh.
3. Recreate layer surface saat `vertical`, `bottom`, atau `cornerStyle` berubah
   lewat config (tanpa restart proses).
4. Uji visual horizontal dan vertical pada 1366×768 + headless 1920×1080.
5. Ukur ulang PSS setelah perubahan.

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
