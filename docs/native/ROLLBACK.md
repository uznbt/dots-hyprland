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

```sh
native/build/dots-session stop
native/build/dots-session backend quickshell
```

`dots-session stop` menghentikan bar/daemon yang dikelolanya dan membuka
kembali bar Quickshell. Kalau ingin memaksa:

```sh
pkill -x dots-bar
pkill -x dotsd
qs -c ii ipc call bar open
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
