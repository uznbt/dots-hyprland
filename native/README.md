# dots-native

Implementasi native C untuk menggantikan Quickshell/QML secara bertahap.

Status: **Fase 1 — bar native multi-output sudah bisa dijalankan, belum menjadi default.**

## Yang sudah ada

- `dots-bar`: bar Wayland C + cairo/pango, hasil port dari `docs/cbar`.
  Satu proses membuat satu layer surface per output, layout dihitung dari
  lebar masing-masing output, mendukung `screenList`, hotplug, auto-hide,
  bar bawah, dan warna Material You dari `generated/colors.json`.
- `dotsd`: daemon IPC Unix socket untuk config dan delegasi aksi panel.
- `dots-ctl`: klien CLI (`ping`, `status`, `config-get`, `config-reload`, `panel`).
- `dots-dispatch`: dispatcher aksi dengan fallback `qs`, untuk migrasi keybind.
- `dots-session`: supervisor tipis untuk mode hybrid; start/stop/status/backend.
- Build Meson + smoke test IPC.
- Skrip uji `scripts/run-bar.sh` yang menyalakan daemon, menutup bar Quickshell
  sementara, dan mengembalikan semuanya saat keluar.
- Aset bar (ikon aplikasi dan spark) di `assets/`.

## Build

```sh
meson setup native/build native
meson compile -C native/build
```

Dependensi saat ini:

- `wayland-client`
- `cairo`
- `pangocairo`
- `pkg-config`, `meson`, `ninja`, compiler C

## Jalankan untuk uji coba

```sh
./native/scripts/run-bar.sh
```

Skrip akan:

1. menutup bar Quickshell (`qs -c ii ipc call bar close`),
2. menambahkan layer rule blur untuk namespace `dots:bar`,
3. menjalankan `native/build/dots-bar`,
4. membuka kembali bar Quickshell saat `dots-bar` berhenti.

Jangan jadikan ini default harian sampai gate G1 lulus. Quickshell masih
menangani sidebar, overview, notifikasi, OSD, dan panel lain.

## Uji multi-output

Semua output aktif otomatis mendapat bar. Untuk menguji hotplug tanpa monitor
fisik kedua:

```sh
hyprctl output create headless
hyprctl layers -j | jq   # cari namespace dots:bar
hyprctl output remove HEADLESS-1
```

`bar.screenList` di config membatasi output yang diberi bar. Variabel
`DOTS_OUTPUT` hanya untuk debugging satu output pada proses terpisah.

## Supervisor hybrid

```sh
./native/build/dots-session status
./native/build/dots-session start      # foreground; jalankan di terminal/exec-once
./native/build/dots-session stop       # kembali ke bar Quickshell
./native/build/dots-session backend hybrid
```

`start` menyalakan `dotsd`, menutup bar Quickshell, lalu menjalankan satu
`dots-bar` yang menangani semua output aktif. Saat proses berhenti atau `stop`
dipanggil, bar Quickshell dibuka kembali. Backend disimpan di
`$XDG_STATE_HOME/dots/backend` (default `hybrid`). Pilihan `quickshell` hanya
membuka kembali bar Quickshell; `native` belum diaktifkan karena panel native
belum ada.

## Daemon dan CLI

```sh
./native/build/dotsd &
./native/build/dots-ctl ping
./native/build/dots-ctl status
./native/build/dots-ctl config-get bar.resources.showMemory
./native/build/dots-ctl panel sidebarRight toggle
./native/build/dots-dispatch sidebarRight toggle
```

`panel` masih mendelegasikan ke Quickshell. Ini adapter transisi, bukan panel
native. Kalau `dotsd` tidak berjalan, `dots-bar` fallback memanggil `qs`
langsung sehingga fitur harian tidak mati.

## Batasan saat ini

- Belum ada bentuk shortened atau vertical.
- Interaksi sidebar/media masih didelegasikan ke Quickshell lewat `dotsd`.
- `dots-session` masih supervisor tipis; belum ada health-check otomatis,
  restart, atau watchdog proses.
- Belum ada tray, popup interaktif penuh, dan animasi auto-hide.
- Warna layer grup/popup border masih konstanta prototipe; warna utama,
  background, teks, dan secondary sudah mengikuti `colors.json`.
- Source `src/ui/bar.c` masih berupa port awal; refactor menjadi modul
  dilakukan setelah fondasi daemon/IPC siap.

## Struktur

```text
native/
  meson.build
  assets/
  protocols/
  scripts/run-bar.sh
  src/common/
  src/daemon/
  src/ctl/
  src/session/
  src/ui/bar.c
```

## Ukuran

Bar native satu output 1366×768:

| Metrik | Nilai |
| --- | ---: |
| PSS | 11,0 MB |
| RSS | 32,1 MB |
| Anonymous | 6,7 MB |
| Private dirty | 6,8 MB |

Dengan dua output (1366×768 + headless 1920×1080) PSS naik ke sekitar
12,9 MB karena ada buffer kedua. `dotsd` (idle, config sudah dimuat): PSS
sekitar 0,4 MB.

Angka ini hanya bar, bukan pengganti total shell. Target dan urutan lengkap
ada di `/home/uznbt/.opencode/plan/rombak-dots-ke-c.md`.
