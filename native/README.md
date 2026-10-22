# dots-native

Implementasi native C untuk menggantikan Quickshell/QML secara bertahap.

Status: **Fase 1 — bar native sudah bisa dijalankan, belum menjadi default.**

## Yang sudah ada

- `dots-bar`: bar Wayland C + cairo/pango, hasil port dari `docs/cbar`.
  Layout tengah dihitung dari lebar output, mendukung auto-hide, bar bawah,
  dan warna Material You dari `generated/colors.json`.
- `dotsd`: daemon IPC Unix socket untuk config dan delegasi aksi panel.
- `dots-ctl`: klien CLI (`ping`, `status`, `config-get`, `config-reload`, `panel`).
- `dots-dispatch`: dispatcher aksi dengan fallback `qs`, untuk migrasi keybind.
- Build Meson + smoke test IPC.
- Skrip uji `scripts/run-bar.sh` yang menyalakan daemon, menutup bar Quickshell
  sementara, dan mengembalikan semuanya saat keluar.
- Aset bar (ikon aplikasi dan spark) di `assets/`.

## Build

```sh
meson setup build native
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

- Layout horizontal sudah dihitung dari lebar output, tetapi belum ada bentuk
  shortened, vertical, atau multi-output.
- Interaksi sidebar/media masih didelegasikan ke Quickshell lewat `dotsd`.
- Belum ada `dots-session`; `run-bar.sh` adalah harness uji, bukan supervisor
  produksi.
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
  src/ui/bar.c
```

## Ukuran

Bar native yang diuji pada 1366×768:

| Metrik | Nilai |
| --- | ---: |
| PSS | 11,0 MB |
| RSS | 32,1 MB |
| Anonymous | 6,7 MB |
| Private dirty | 6,8 MB |

`dotsd` (idle, config sudah dimuat): PSS sekitar 0,4 MB.

Angka ini hanya bar, bukan pengganti total shell. Target dan urutan lengkap
ada di `/home/uznbt/.opencode/plan/rombak-dots-ke-c.md`.
