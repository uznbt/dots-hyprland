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
