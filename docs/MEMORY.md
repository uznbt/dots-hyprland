# 🧠 Analisis Memori Shell (Quickshell / illogical-impulse)

Diukur langsung di mesin ini (i5-8250U / 8 GB / Intel UHD 620), 21 Oktober 2026.
Semua angka di bawah adalah **PSS** (proportional set size): bagian memori yang
benar-benar dipakai proses ini, bukan yang di-share dengan proses lain.

## 1. Ringkasan

| Komponen | PSS | Catatan |
| --- | ---: | --- |
| Quickshell kosong (jendela tanpa isi) | **59 MB** | lantai Qt + Quickshell |
| + seluruh service yang start (theme, wallpaper, updates, cliphist) | **72 MB** | |
| + modul ii di-import (tanpa panel dibuat) | **72 MB** | import modul itu murah |
| + panel **bar** | **139 MB** | bar paling berat, ~80 MB |
| + panel **sidebar kiri** | **127 MB** | launcher + chat AI, ~68 MB |
| + panel **sidebar kanan** | **127 MB** | ~68 MB |
| Shell ii utuh, saat start | **~245 MB** (RSS 335 MB) | |
| Shell setelah ~15 reload | **~300–320 MB** | tiap reload menahan ~3 MB |

Kesimpulan: **bukan QML-nya yang mahal, tapi objek yang tetap hidup**.
Bar + dua sidebar saja sudah menyumbang ~150 MB.

## 2. Yang sudah diperbaiki

| Perubahan | Efek |
| --- | --- |
| Indikator RAM/swap/CPU punya switch `show*` | Indikator yang dimatikan **tidak dipoll** lagi (`/proc/meminfo`, `/proc/stat` tidak dibaca) dan tidak ada history yang disimpan |
| Popup bar + overlay OSD | Hanya menampilkan indikator yang aktif |
| Sidebar kiri di-unload saat ditutup (`sidebar.keepLeftSidebarLoaded`) | Hemat **~46 MB** saat sidebar tertutup (dulu isinya selalu di memori) |
| `checkupdates` dijalan dengan `nice -n 19 ionice -c3` | Copies database pacman tidak lagi mengganggu |
| Cek update pertama ditunda 2 menit | Shell yang baru start tidak langsung kena lonjakan RAM |
| `resources.updateInterval` 5000 → 10000 ms | Polling separuh lebih sering |
| `resources.historyLength` 60 → 30 | History RAM/CPU/swap setengahnya |

## 3. Optimasi lanjutan (putaran kedua)

| Perubahan | Efek |
| --- | --- |
| Family panel `waffle` (~140 file QML) tidak lagi dikompilasi saat start | hemat ±15–25 MB; tetap bisa dipakai kalau `panelFamily` diganti |
| Uptime hanya diperbarui tiap 60 detik (dulu 10 detik, baca `/proc/uptime` terus) | lebih sedikit I/O & alokasi |
| Watchdog `nmcli monitor` cek tiap 5 detik (dulu 2 detik) | setengah spawn proses |
| Cek update tiap 6 jam (dulu 2 jam) | lebih jarang lonjakan pacman DB |
| Indikator RAM/swap/CPU yang dimatikan tidak dipoll | seperti sebelumnya |

Catatan penting soal angka: PSS shell **naik-turun 100+ MB antar restart** di mesin ini
(sistem sedang pakai swap). Yang bisa dibandingkan hanya angka dari proses yang sama,
bukan antar restart. Yang paling bisa diandalkan: jumlah objek/QML yang dibuat.

Selain itu, tiap **reload** (bukan restart) menahan sekitar 3–5 MB. Kalau shell sudah
lama hidup dan sering di-reload, restart penuh sekali akan mengembalikan memori itu.

## 3. Yang masih bisa ditarik (urut dari yang paling murah)

| Lever | Potensi | Cara |
| --- | --- | --- |
| Matikan indikator yang tidak dipakai | ±2–5 MB +/− polling | `SUPER+I` → Bilah → Indikator sumber daya |
| Sidebar kanan jangan dimuat | ±15 MB | `SUPER+I` → Antarmuka → *Keep right sidebar loaded* (sudah `false`) |
| Jangan pakai sidebar kiri | ±46 MB | `SUPER+I` → Antarmuka → *Keep left sidebar loaded* (sekarang `false`) |
| Kurangi interval cek update | hindari lonjakan ~30–50 MB tiap cek | `updates.checkInterval` (menit) di config |
| Matikan widget bar yang tidak dipakai (weather, jam, tray) | beberapa MB tiap widget | config `bar.*` |
| Restart shell sesekali | ~3 MB per reload yang tertahan | reload memang menahan memori |
| Chromium/Electron | ~1–1,5 GB | sudah ada wrapper low-RAM, lihat `docs/RAM.md` |

## 4. Cara mengukur sendiri

```sh
# PSS shell (memori yang benar-benar dipakai)
awk '/^Pss:/ {print $2" kB"}' /proc/$(pgrep -x qs | head -1)/smaps_rollup

# RSS (termasuk yang di-share dengan proses lain)
awk '/^Rss:/ {print $2" kB"}' /proc/$(pgrep -x qs | head -1)/smaps_rollup

# child process dari shell (harus cuma satu: monitor NM)
ps --ppid $(pgrep -x qs | head -1) -o pid,rss,comm
# cek lonjakan dari proses lain
ps -eo rss,comm --no-headers | awk '{r[$2]+=$1} END {for (i in r) printf "%8.0f MB  %s\n", r[i]/1024, i}' | sort -rn | head -10
```

## 5. Catatan

* Angka PSS naik sekitar 3 MB tiap reload. Itu karena Qt menyimpan halaman yang
  di-free di dalam heap dan tidak dikembalikan ke OS. Kalau RAM habis,
  restart shell (bukan reload) lebih efektif.
* Menonaktifkan indikator di bar **tidak** mematikan OSD/overlay: keduanya
  memakai flag yang sama, jadi ikut hilang.
* `checkupdates` menyalin database pacman ke `/tmp`; saat berjalan dia
  boros beberapa puluh MB. Karena itu dijalan dengan prioritas terendah dan
  cek pertama ditunda.

## 6. Arsitektur panel lazy (putaran ketiga)

Sebelumnya semua `PanelLoader` di `panelFamilies/IllogicalImpulseFamily.qml`
aktif begitu `Config.ready`, jadi 22 panel dibuat sekaligus walau belum pernah
dibuka. Sekarang 9 panel baru dibuat saat **pertama kali dipakai**:

`cheatsheet`, `displayPanel`, `mediaControls`, `onScreenKeyboard`, `overlay`,
`regionSelector`, `screenTranslator`, `sessionScreen`, `wallpaperSelector`.

Cara kerjanya:

1. **`panelFamilies/PanelLoader.qml`** punya `lazy`, `open`, dan `everLoaded`.
   `active` baru bernilai true saat `open` (flag `GlobalStates.*Open`) true.
   Setelah aktif sekali, `everLoaded` menahan panel tetap hidup supaya
   tutup–buka berikutnya instan dan animasinya tidak terpotong.
2. **`panelFamilies/PanelStubs.qml`** selalu dimuat dan berisi semua
   `IpcHandler` + `GlobalShortcut` untuk panel lazy. Isinya hanya mengubah
   flag `GlobalStates` atau memanggil service — tidak menyentuh state internal
   panel, jadi panelnya sendiri boleh belum ada.
3. Handler di dalam panel-panel tersebut dihapus supaya tidak ada target IPC
   ganda. Panel tetap bereaksi terhadap flag yang sama seperti sebelumnya.

Hasil pengukuran (PSS, 30–35 detik setelah restart bersih):

| Konfigurasi | PSS | Anon |
| --- | ---: | ---: |
| Semua panel eager (sebelum) | 375.5 MB | 294 MB |
| 9 panel lazy (sesudah) | **354.7 MB** | **285.8 MB** |

Hemat **~21 MB** di startup. Setelah sebuah panel dibuka sekali, memori
kembali ke ukuran panel itu (memang disengaja, supaya tidak rebuild terus).

Catatan penting untuk eksperimen berikutnya:

* Memuat panel lewat `source: "file://" + Quickshell.shellPath(...)` **tidak
  bisa** dipakai untuk panel ii: import `qs.modules.ii.*` di dalam file itu
  gagal (`module ... is not installed`). Yang bekerja adalah tetap memakai
  `component:` (QML ikut dikompilasi bersama family) dan menunda
  **instansiasi**-nya lewat `active` seperti di atas. Biaya kompilasi QML
  kecil; yang mahal adalah objek/window/timer yang dibuat.
* Jangan memberi `requiredCondition` untuk overlay yang membaca
  `OverlayContext.hasPinnedWidgets`: pinned widget hanya bisa dibuat dari
  overlay itu sendiri, jadi membuka overlay sekali sudah menahan loader-nya
  tetap hidup.

