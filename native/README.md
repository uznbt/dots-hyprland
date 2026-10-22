# dots-native

Implementasi native C untuk menggantikan Quickshell/QML secara bertahap.

Status: **Fase 3 — bar native multi-output, OSD volume/brightness, dan
notification server/popup sudah bisa dijalankan, belum menjadi default.**

## Yang sudah ada

- `dots-bar`: bar Wayland C + cairo/pango, hasil port dari `docs/cbar`.
  Satu proses membuat satu layer surface per output, layout dihitung dari
  lebar masing-masing output, mendukung bentuk penuh/shortened (≤1200 px dan
  ≤1000 px), bar horizontal/vertical, corner style hug/float/plain,
  `screenList`, hotplug, auto-hide, bar bawah, dan warna Material You dari
  `generated/colors.json`.
- `dotsd`: daemon IPC Unix socket untuk config dan delegasi aksi panel.
- `dots-ctl`: klien CLI (`ping`, `status`, `config-get`, `config-reload`, `panel`).
- `dots-dispatch`: dispatcher aksi dengan fallback `qs`, untuk migrasi keybind;
  volume/brightness diarahkan ke `dots-osd` bila tersedia.
- `dots-session`: supervisor tipis untuk mode hybrid; start/stop/status/backend.
- `dots-osd`: overlay OSD volume/brightness native. Proses hanya hidup selama
  overlay tampil; invocation berikutnya mengirim command lewat Unix datagram
  socket ke instance aktif. Ada fade in/out dan timeout dari `osd.timeout`.
- `dots-notify`: server D-Bus `org.freedesktop.Notifications` dan popup native
  dalam satu proses. Mendukung replace ID, actions, timeout, urgency, hover
  pause, expand/collapse, close, dan warna Material You. Server bersifat opt-in
  dan tidak boleh berjalan bersamaan dengan notification server Quickshell.
- Build Meson + smoke test IPC/OSD/notifikasi.
- Skrip uji `scripts/run-bar.sh` yang menyalakan daemon, menutup bar Quickshell
  sementara, dan mengembalikan semuanya saat keluar.
- Aset bar (ikon aplikasi dan spark) di `assets/`.

## Build

```sh
meson setup native/build native
meson compile -C native/build
meson test -C native/build --print-errorlogs
```

Dependensi saat ini:

- `wayland-client`
- `cairo`
- `pangocairo`
- `json-c`
- `glib-2.0`
- `gio-2.0`
- `pkg-config`, `meson`, `ninja`, compiler C

Runtime untuk OSD: `wpctl` (PipeWire), `brightnessctl`, dan opsional
`hyprctl` untuk memilih monitor fokus. `dots-notify` memerlukan session bus
D-Bus dan (untuk tes) `dbus-daemon`, `gdbus`, `dbus-monitor`, `notify-send`.
`qs` hanya dipakai sebagai bridge transisi dan fallback.

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
menangani sidebar, overview, notifikasi, media, dan panel lain.

## OSD volume/brightness

```sh
./native/build/dots-osd show volume 0.42
./native/build/dots-osd show brightness 0.20
./native/build/dots-osd volume up
./native/build/dots-osd volume down
./native/build/dots-osd volume mute
./native/build/dots-osd brightness up
./native/build/dots-osd brightness down
./native/build/dots-osd hide
```

Proses pertama membuka layer surface di monitor fokus, menggambar kartu, lalu
keluar setelah timeout. Invocation berikutnya tidak membuat proses kedua:
command dikirim ke socket `$XDG_RUNTIME_DIR/dots/osd.sock` dan instance yang
sedang tampil memperbarui nilainya. `DOTS_OSD_TIMEOUT_MS` menimpa timeout untuk
tes, dan `DOTS_OUTPUT` memaksa output tertentu.

`dots-dispatch` adalah jalur yang dipakai keybind:

```sh
./native/build/dots-dispatch volume up
./native/build/dots-dispatch brightness down
```

Kalau `dots-osd` tidak ditemukan, dispatcher jatuh ke `wpctl`/Quickshell.
Brightness native hanya dipakai untuk output internal (`eDP`/`LVDS`/`DSI`);
monitor eksternal tetap lewat Quickshell agar ddcutil tidak kehilangan
fungsinya. Keybind di repo memakai bentuk `dots-dispatch ... || fallback`, jadi
keybind tetap berfungsi saat binary native belum ada di `PATH`. Selama OSD
native tampil, `dots-osd` menonaktifkan sementara OSD Quickshell lewat
`osdVolume nativeOn/nativeOff`; timer pengaman di Quickshell mengaktifkannya
kembali kalau proses native mati mendadak.

## Notifikasi native

`dots-notify` menggabungkan server D-Bus dan popup. Server **tidak** dinyalakan
oleh `dots-session` selama Quickshell masih menjadi notification server. Untuk
tes aman, jalankan session bus privat:

```sh
TMP=$(mktemp -d)
ADDR="unix:path=$TMP/bus"
dbus-daemon --session --address="$ADDR" --nofork --nopidfile &
DBUS_SESSION_BUS_ADDRESS="$ADDR" ./native/build/dots-notify serve &
DBUS_SESSION_BUS_ADDRESS="$ADDR" notify-send -t 0 "Judul" "Isi notifikasi"
```

Popup muncul di kanan atas monitor fokus. Saat notifikasi baru datang,
`dots-notify` memilih ulang monitor fokus dan memindahkan popup bila perlu.
Klik kanan kartu untuk expand, klik tengah untuk dismiss, dan tombol
`Close`/aksi untuk menutup atau menjalankan action. `notifications.timeout`
dipakai bila pengirim meminta timeout `-1`; nilai `0` berarti notifikasi tidak
kedaluwarsa. `DOTS_OUTPUT` memaksa output tertentu untuk tes multi-monitor.

Server hanya untuk satu pemilik nama D-Bus. Jangan menjalankan `dots-notify`
saat Quickshell masih memegang `org.freedesktop.Notifications`. Untuk tes
D-Bus tanpa Wayland:

```sh
./native/build/dots-notify serve --no-ui
meson test -C native/build notify-smoke --print-errorlogs
```

Belum ada history persisten, DND, grouping per aplikasi, body image/markup,
atau animasi fade. Ikon aplikasi hanya dibaca dari PNG (path langsung atau
nama di direktori hicolor); SVG belum didukung.

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

- Tray StatusNotifierItem belum ada.
- Popup bar (jam/resource/baterai) masih non-interaktif; popup vertical
  ditempatkan di samping bar, bukan jendela terpisah.
- Interaksi sidebar/media masih didelegasikan ke Quickshell lewat `dotsd`.
- OSD baru volume/brightness backlight internal. ddcutil/monitor eksternal,
  rotasi/scale ikon brightness, dan lokalisasi label belum ada.
- `dots-osd` memilih monitor fokus saat OSD mulai; belum mengikuti perpindahan
  fokus selama overlay masih tampil.
- `dots-notify` belum punya history persisten, DND, grouping per aplikasi,
  image body, ikon SVG, atau fade. Server belum dijalankan otomatis dan belum
  diintegrasikan ke `dots-session`.
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
  src/ui/osd.c
  src/ui/notify.c
  tests/test-ipc.sh
  tests/test-osd.sh
  tests/test-notify.sh
```

## Ukuran

Bar native satu output 1366×768:

| Metrik | Nilai |
| --- | ---: |
| PSS | 11,3 MB |
| RSS | 41,4 MB |
| Anonymous | 6,4 MB |
| Private dirty | 6,4 MB |

Dengan dua output (1366×768 + headless 1920×1080) PSS naik ke sekitar
12,5 MB karena ada buffer kedua. `dotsd` (idle, config sudah dimuat): PSS
sekitar 0,4 MB.

OSD diukur saat kartu volume tampil (1366×768):

| Metrik | Nilai |
| --- | ---: |
| PSS | 9,3 MB |
| RSS | 35,9 MB |
| Anonymous | 4,6 MB |
| Private dirty | 4,6 MB |

Saat OSD tidak tampil, tidak ada proses `dots-osd`; invocation berikutnya
membuka proses baru dan mengirim command ke socket bila sudah ada instance.

Notifikasi (`dots-notify`, server + popup, 1366×768):

| Metrik | Nilai |
| --- | ---: |
| PSS baru start | 0,6 MB |
| PSS idle setelah popup pernah tampil | 9,9 MB |
| RSS idle setelah popup | 37,0 MB |
| Anonymous idle setelah popup | 4,9 MB |
| PSS dengan 3 kartu terlihat | 10,1 MB |

Server tetap hidup untuk menerima D-Bus sehingga PSS idle tidak kembali ke
0. Sebagian besar kenaikan berasal dari pustaka Cairo/Pango dan state font
yang sudah disentuh. Angka ini bukan pengganti total shell. Target dan urutan
lengkap ada di `/home/uznbt/.opencode/plan/rombak-dots-ke-c.md`.
