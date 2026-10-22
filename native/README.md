# dots-native

Implementasi native C untuk menggantikan Quickshell/QML secara bertahap.

Status: **Fase 1 — bar native sudah bisa dijalankan, belum menjadi default.**

## Yang sudah ada

- `dots-bar`: bar Wayland C + cairo/pango, hasil port dari `docs/cbar`.
- Build Meson.
- Skrip uji `scripts/run-bar.sh` yang menutup bar Quickshell sementara dan
  mengembalikannya saat keluar.
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

## Batasan saat ini

- Geometri masih membawa konstanta prototipe 1366×768.
- Interaksi sidebar/media masih memanggil Quickshell lewat `qs ... ipc call`.
- Belum ada `dotsd`, `dots-ctl`, atau `dots-session`.
- Belum ada multi-output, auto-hide, tray, dan popup interaktif penuh.
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
| PSS | 10,9 MB |
| RSS | 31,8 MB |
| Anonymous | 6,6 MB |
| Private dirty | 6,7 MB |

Angka ini hanya bar, bukan pengganti total shell. Target dan urutan lengkap
ada di `/home/uznbt/.opencode/plan/rombak-dots-ke-c.md`.
