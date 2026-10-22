<div align="center">
    <h1>【 uznbt's Hyprland dotfiles 】</h1>
    <h3>ilogical-impulse (ii) — Quickshell edition</h3>
</div>

<div align="center">

![repo-size](https://img.shields.io/github/repo-size/uznbt/dots-hyprland?color=86dbce&label=size&logo=git&logoColor=D9E0EE&labelColor=1E202B)

</div>

<div align="center">
    <h2>• what's this? •</h2>
</div>

A personal Hyprland configuration built on top of
[end-4's illogical-impulse](https://github.com/end-4/dots-hyprland) — the
Quickshell-based widget shell. Kept as a separate repo so the upstream can be
pulled from freely while these personal tweaks stay under my control.

| Component | What it is |
| --------- | ---------- |
| [Hyprland](https://github.com/hyprwm/hyprland) | The compositor (window manager) |
| [Quickshell](https://quickshell.outfoxxed.me/) | QtQuick-based widget system: bar, sidebars, overview, dock |
| `sdata/` | Install / update helper scripts (`./setup install`) |

<div align="center">
    <h2>• my additions over upstream •</h2>
</div>

- **Opencode-powered sidebar AI** — the chat panel can drive a local
  [opencode](https://opencode.ai) agent instead of a plain chat completion
  (`services/ai/OpencodeApiStrategy.qml`, `services/Ai.qml`).
- **Display panel** — a dedicated panel for display/monitor settings
  (`modules/ii/displayPanel/`, `services/DisplayService.qml`).
- **Overview app grid** — app list with live previews in the overview
  (`modules/ii/overview/OverviewApps.qml`).
- **Wallpaper transitions** — fade / circle / zoom / swipe reveals between
  wallpapers, with a random-effect mode (`modules/ii/background/Background.qml`).
- **Hyprland Lua config** — `hyprland.conf` split into `general.lua`,
  `keybinds.lua`, `rules.lua`, `execs.lua`, plus a `~/.config/hypr/custom/`
  overlay that survives upstream updates.
- **Housekeeping scripts** — `clean_chromium.sh`, `clean_sober.sh` for wiping
  browser/app traces.
- **Lower-RAM profile** — see [RAM notes](#-lower-ram-profile) below.
- **C bar prototype** — an experimental standalone C/cairo replica of the bar
  (clicks, scroll, hover popups, notification bell) used to measure the real
  cost of the QML bar. It is *not* enabled by default; details in
  [`docs/cbar/`](docs/cbar/README.md).

<div align="center">
    <h2>• install •</h2>
</div>

```bash
git clone --recurse-submodules https://github.com/uznbt/dots-hyprland.git
cd dots-hyprland
./setup install
```

Already cloned without submodules? Run:

```bash
git submodule update --init --recursive
```

<div align="center">
    <h2>• lower-RAM profile •</h2>
</div>

This machine runs a 8 GB / 7.5 GiB box, so the shell is tuned to stay lean:

- The settings app is opened **inside** the running shell via IPC instead of
  spawning a second full Quickshell process (that alone was ~400 MB).
- Rarely used panels are unloaded when closed instead of being kept alive.
- `ResourceUsage` polling interval and update checks are relaxed.

Full details and the reasoning live in [`docs/RAM.md`](docs/RAM.md).

<div align="center">
    <h2>• keybinds •</h2>
</div>

| Shortcut | Action |
| -------- | ------ |
| `Super` + `/` | Keybind / cheatsheet list |
| `Super` + `Tab` | Toggle overview (workspace switcher) |
| `Super` + `A` / `N` | Toggle left / right sidebar |
| `Super` + `I` | Open settings (in-shell) |
| `Super` + `M` | Media controls |
| `Super` + `G` | Widget overlay |
| `Super` + `Ctrl` + `Delete` | Session menu (log out / power off) |

<div align="center">
    <h2>• credits •</h2>
</div>

This configuration is a fork of [end-4/dots-hyprland](https://github.com/end-4/dots-hyprland)
(illogical-impulse), licensed under the [GPL-3.0](LICENSE). Huge thanks to
[@end-4](https://github.com/end-4), [@clsty](https://github.com/clsty) and the
whole illogical-impulse community.

<div align="center">
    <h2>• stonks •</h2>
</div>

If this setup was useful to you, consider starring
[end-4/dots-hyprland](https://github.com/end-4/dots-hyprland) — the upstream
author did the heavy lifting here.
