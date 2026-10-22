---
name: dots-hyprland
description: Work on the dots-hyprland repository, especially the native C/Wayland shell migration and Quickshell parity. Use for the bar, panels, daemons, Meson build, memory measurements, Hyprland integration, session handoff, or any task in this repo. Contains the project map, current checkpoint, commands, and hard constraints.
---

# dots-hyprland — peta kerja

Baca skill ini sebelum menjelajah repo. Tujuannya: agent langsung tahu lokasi file,
status terakhir, dan batas aman, tanpa scan seluruh repo atau memasang MCP.

## 1. Baca dulu (maksimal 3 file)

1. `docs/native/STATUS.md` — checkpoint kerja terakhir, langkah berikutnya.
2. `docs/native/PARITY.md` — kontrak fitur native vs Quickshell.
3. `/home/uznbt/.opencode/plan/rombak-dots-ke-c.md` — rencana besar; baca bagian
   yang relevan saja (fase/gate), jangan seluruh file.

Status commit terakhir: `git log -1 --oneline` dan `git status --short`.

## 2. Batas keras

- **Jangan ubah** `dots/.config/quickshell/ii/modules/common/Persistent.qml` dan
  `.../ii/timerIndicator/TimerIndicator.qml`. Keduanya milik user dan sedang
  dimodifikasi. Perlakukan sebagai read-only.
- Quickshell tetap fallback sampai gate native daily driver lolos. Jangan hapus
  atau mematikan jalur Quickshell.
- Jangan menambah MCP server, map/token-heavy integration, atau tool baru hanya
  untuk eksplorasi. Gunakan `rg`, `git`, dan file proyek.
- Jangan commit perubahan user yang tidak terkait. Stage file spesifik.
- Perubahan perilaku harus dicatat di `docs/native/PARITY.md` sebelum dianggap
  selesai.
- Ukur memori dengan `/proc/<pid>/smaps_rollup`, bukan perkiraan.

## 3. Peta repo

| Path | Isi |
| --- | --- |
| `native/src/ui/bar.c` | Bar Wayland + cairo/pango, satu surface per output. File besar (~3k baris); baca potongan dengan `rg`/`sed`. |
| `native/src/daemon/dotsd.c` | Daemon IPC Unix socket, config, delegasi panel. |
| `native/src/ctl/` | `dots-ctl`, `dots-dispatch`. |
| `native/src/session/dots-session.c` | Supervisor hybrid/rollback. |
| `native/src/common/` | Config JSON-C, IPC, paths, log. |
| `native/scripts/run-bar.sh` | Jalankan bar native, tutup bar Quickshell sementara. |
| `native/tests/test-ipc.sh` | Smoke test IPC. |
| `docs/native/PARITY.md` | Status fitur + gate. |
| `docs/native/STATUS.md` | Checkpoint sesi; update di akhir kerja. |
| `dots/.config/quickshell/ii/` | Shell Quickshell `ii`; sumber perilaku yang diport. |
| `dots/.config/hypr/` | Config Hyprland (Lua), keybind, exec. |
| `dots/.config/illogical-impulse/config.json` | Config live user. Backup sebelum diubah untuk tes. |
| `native/assets/` | Ikon bar. |

Arsitektur target: `dotsd` (state/service) + `dots-shell`/`dots-bar` (UI Wayland) +
`dots-settings` (on demand) + `dots-ctl` + `dots-session`. Detail di rencana.

## 4. Perintah penting

Build dan test:

```sh
meson setup native/build native          # sekali saja
meson compile -C native/build
meson test -C native/build --print-errorlogs
```

Jalankan untuk uji coba (menutup bar Quickshell sementara):

```sh
./native/scripts/run-bar.sh
```

Jalankan langsung tanpa menyentuh Quickshell:

```sh
./native/build/dots-bar
```

Memori:

```sh
pid=$(pgrep -x dots-bar | head -1)
grep -E '^(Rss|Pss|Anonymous|Private_Dirty):' /proc/$pid/smaps_rollup
```

Screenshot dan multi-output headless:

```sh
grim -o eDP-1 /tmp/opencode/bar.png
hyprctl output create headless
hyprctl monitors -j | jq -r '.[] | [.name,.width,.height] | @tsv'
hyprctl output remove HEADLESS-1
```

Tes config horizontal/vertical/float tanpa merusak config user:

```sh
cfg="$HOME/.config/illogical-impulse/config.json"
cp "$cfg" /tmp/opencode/config.json.bak
jq '.bar.vertical = true' "$cfg" > /tmp/opencode/config.json &&
  mv /tmp/opencode/config.json "$cfg"
# ... tes ...
mv /tmp/opencode/config.json.bak "$cfg"
```

## 5. Jebakan yang sudah ditemui

- Hyprland memakai parser Lua baru: `hyprctl keyword monitor ...` gagal.
  Gunakan `hyprctl eval 'hl.monitor({...})'`.
- `wlr-randr` tidak terpasang. Ubah mode headless lewat `hyprctl eval`.
- Bar Quickshell ditutup dengan `qs -c ii ipc call bar close`; buka lagi dengan
  `qs -c ii ipc call bar open`.
- `native/src/ui/bar.c` menyimpan state per-output sebagai "scratch" di
  `struct state`; `view_load()`/`view_save()` menyalin field. Setiap field view
  baru harus ditambahkan di kedua fungsi itu.
- Popup bar horizontal digambar di surface yang sama; surface sengaja lebih
  tinggi dari bar. Jangan menganggap surface = tebal bar.
- Ikon dan font bergantung pada `Material Symbols Rounded`, `Pango`, dan aset di
  `native/assets/`.
- Config dibaca ulang tiap detik lewat `refresh_sidebars()` → `read_config()`.
  Perubahan orientasi/cornerStyle perlu recreate surface, bukan hanya redraw.

## 6. Alur kerja yang hemat token

1. `git status --short` dan `git log --oneline -3` — pastikan tidak menimpa kerja user.
2. Baca `docs/native/STATUS.md`; tentukan satu langkah kecil yang bisa selesai.
3. Cari target dengan `rg -n` dan baca hanya rentang baris yang relevan.
4. Edit, build, test, ukur.
5. Perbarui `docs/native/PARITY.md` dan `docs/native/STATUS.md`.
6. Commit file spesifik dengan pesan `feat(native): ...` / `fix(native): ...`.
7. Kalau sesi berakhir di tengah jalan, tulis checkpoint di `STATUS.md`
   (jangan hanya mengandalkan ringkasan chat).

## 7. Skill pendamping

- `handoff` — ringkas sesi untuk agent berikutnya (output ke `/tmp`).
- `diagnosing-bugs` — loop diagnosis untuk bug/regresi sulit.
- `documentation-and-adrs` — catat keputusan arsitektur yang mahal dibalik.
- `memory-safety-patterns` — checklist resource/leak saat menulis C.

Skill pendamping dipasang project-local dan dicatat di `skills-lock.json`.
Direktori hasil install (`.agents/`) diabaikan git; setelah clone baru, pulihkan
dengan `npx skills experimental_install`. Skill proyek ini sendiri ada di
`.opencode/skills/dots-hyprland/` dan ikut ter-commit.
