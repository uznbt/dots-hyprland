# Rollback Native Shell

Selama Fase 1–2, Quickshell tetap menjadi backend default. Native bar hanya
dijalankan lewat skrip uji.

## Kembali ke bar Quickshell

```sh
pkill -x dots-bar
qs -c ii ipc call bar open
```

`native/scripts/run-bar.sh` sudah melakukan ini otomatis lewat `trap` saat
proses berhenti.

## Kembali ke seluruh shell Quickshell

Jika nanti `dots-session` sudah ada:

```sh
dots-session backend quickshell
dots-session restart
```

Selama Fase 1 belum ada `dots-session`, jalankan:

```sh
pkill -x dots-bar
qs -c ii
```

## Baseline git

- Tag: `legacy/quickshell-2026-10`
- Branch kerja: `native/c-shell`
- Backup konfigurasi dan mod user:
  `/tmp/opencode/dots-native-baseline/`

Backup berisi:

- `user-mods.patch` — diff `Persistent.qml` dan `TimerIndicator.qml`
- `config.json` — config live
- `todo.json` — state todo
- `notifications/` — state notifikasi

Jangan menghapus folder backup itu sampai native shell stabil.
