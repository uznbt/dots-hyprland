<div align="center">
    <h1>uznbt's Hyprland dotfiles</h1>
    <p>A personal fork of <a href="https://github.com/end-4/dots-hyprland">end-4/dots-hyprland</a> (illogical-impulse), with an experimental native C/Wayland shell.</p>
    <p>
        <img alt="repo size" src="https://img.shields.io/github/repo-size/uznbt/dots-hyprland?color=86dbce&label=size&logo=git&logoColor=D9E0EE&labelColor=1E202B">
        <img alt="license" src="https://img.shields.io/badge/license-GPL--3.0-blue">
    </p>
</div>

## What this is

This is my personal Hyprland setup. It is built on top of
[end-4's illogical-impulse](https://github.com/end-4/dots-hyprland), but it is
not a general-purpose distribution and it is not affiliated with upstream.

The fork exists so I can keep personal changes under version control while
still pulling from upstream. Most of the Quickshell shell is upstream work; the
main additions here are personal tweaks, a lower-RAM profile, and an
experimental native C/Wayland replacement for parts of Quickshell.

There are two tracks in this repository:

| Track | Branch | Status |
| --- | --- | --- |
| Quickshell `ii` | `main` | Stable, default daily driver |
| Native C/Wayland shell | `native/c-shell` | Experimental, not a daily driver yet |

**Quickshell remains the fallback until the native daily-driver gate passes.**
Do not remove the Quickshell path or switch the session backend to `native`.

## Fork additions

- **Native C/Wayland shell** — a standalone bar, daemon, OSD, and notification
  server written in C. It is being ported from Quickshell incrementally.
- **Fingerprint unlock in Quickshell** — the `ii` lock screen detects enrolled
  fingerprints for the current OS user and starts PAM fingerprint authentication.
- **OpenCode-powered sidebar AI** — the chat panel can drive a local
  [OpenCode](https://opencode.ai) agent instead of a plain chat completion.
- **Display panel** — a dedicated panel for display and monitor settings.
- **Overview app grid** — app list with live previews in the overview.
- **Wallpaper transitions** — fade, circle, zoom, and swipe reveals, plus a
  random-effect mode.
- **Hyprland Lua config** — the config is split into Lua modules, with a
  `~/.config/hypr/custom/` overlay that survives upstream updates.
- **Housekeeping scripts** — `clean_chromium.sh` and `clean_sober.sh` for
  clearing browser/app traces.
- **Lower-RAM profile** — panels are unloaded when closed, settings open
  in-shell, and resource polling is relaxed.

## Native shell status

The native track is usable for testing, but it does not replace the full
Quickshell desktop yet.

| Component | Current state |
| --- | --- |
| `dots-bar` | Wayland/cairo bar; horizontal and vertical, multi-output, hotplug, auto-hide, shortened forms, corner styles |
| `dotsd` | IPC daemon and config service |
| `dots-ctl` / `dots-dispatch` | CLI and action dispatcher with Quickshell fallback |
| `dots-session` | Hybrid supervisor and rollback path |
| `dots-osd` | Native volume/brightness OSD; transient process with socket singleton |
| `dots-notify` | D-Bus notification server + native popup; opt-in only |
| Media, dock, session menu, display panel, wallpaper, native lock | Not ported yet; still Quickshell |

Important constraints:

- Do **not** run `dots-notify` while Quickshell owns
  `org.freedesktop.Notifications`. Both cannot own the same bus name.
- Do **not** enable `dots-session backend native` until the native panels are
  ready. Use `hybrid` or keep Quickshell.
- The native notification server and OSD are not autostarted by default.
- The native bar is tested on this machine only; treat it as experimental.

Build and run instructions are in [`native/README.md`](native/README.md).
Current parity and measurements are in
[`docs/native/PARITY.md`](docs/native/PARITY.md), and the live checkpoint is in
[`docs/native/STATUS.md`](docs/native/STATUS.md).

## Fingerprint unlock

Fingerprint unlock uses the Quickshell `ii` lock screen, not Hyprlock. Keep
`lock.useHyprlock` set to `false` if you want the Quickshell path.

On this machine the sensor is a Synaptics `06cb:009a` (ThinkPad L480). It needs
the `python-validity` + `open-fprintd` stack; the regular `libfprint` driver
does not support this sensor properly. The tested stack is:

- `python-validity`
- `open-fprintd`
- `fprintd-clients-git`

Only one process should own `net.reactivated.Fprint`. Do not run another
`fprintd` daemon alongside `open-fprintd`.

Quickshell checks `fprintd-list "$(whoami)"` when it starts and again whenever
the screen is locked. The fingerprint icon appears only when the current OS
user has at least one enrolled fingerprint. The password field remains the
fallback, and the PAM configuration lives at
`modules/common/panels/lock/pam/fprintd.conf`.

### Dual-boot note

The fingerprint sensor stores templates for both Windows and Linux in the same
internal database. If the same finger is enrolled in both operating systems,
the sensor can match the Windows record first and Linux will report
`verify-no-match`.

For a dual-boot setup, use a **different finger for each OS**. For example:

- Linux: right index finger
- Windows: right middle finger

Do not enroll the same finger in both operating systems if you want both
fingerprint logins to work reliably. Windows account passwords and PINs are not
affected by changing fingerprint enrollment.

## Install

```bash
git clone --recurse-submodules https://github.com/uznbt/dots-hyprland.git
cd dots-hyprland
./setup install
```

Already cloned without submodules?

```bash
git submodule update --init --recursive
```

The upstream installer and documentation still apply for most of the setup.
See [`sdata/README.md`](sdata/README.md) and the
[upstream repository](https://github.com/end-4/dots-hyprland) for details.

### Native shell build

The native track is separate from the normal Quickshell install:

```bash
meson setup native/build native
meson compile -C native/build
meson test -C native/build --print-errorlogs
```

To try the native bar without making it the default:

```bash
./native/scripts/run-bar.sh
```

The script temporarily closes the Quickshell bar, starts the native bar, and
restores Quickshell when it exits. Dependencies and test instructions are in
[`native/README.md`](native/README.md).

## Keybinds

`Super` + `/` opens the in-shell cheatsheet, which is the quickest way to see
the full active list. This fork also loads
`~/.config/hypr/custom/keybinds.lua` after the defaults; the overrides shipped
here make `Super` + `Tab` switch workspaces and add `Alt` + `Tab` window
cycling.

### Shell

| Shortcut | Action |
| --- | --- |
| `Super` + `/` | Cheatsheet |
| `Super` + `A` | Toggle left sidebar |
| `Super` + `N` | Toggle right sidebar |
| `Super` + `M` | Media controls |
| `Super` + `G` | Widget overlay |
| `Super` + `K` | On-screen keyboard |
| `Super` + `J` | Toggle bar |
| `Super` + `P` | Display panel |
| `Ctrl` + `Super` + `T` | Change wallpaper |
| `Ctrl` + `Super` + `Alt` + `T` | Random wallpaper |
| `Ctrl` + `Super` + `Shift` + `D` | Toggle light/dark mode |
| `Ctrl` + `Super` + `P` | Cycle panel family |
| `Ctrl` + `Super` + `R` | Restart widgets |
| Double-tap `Super` | Toggle search |

### Workspaces and windows

| Shortcut | Action |
| --- | --- |
| `Super` + `Tab` | Next workspace |
| `Super` + `Shift` + `Tab` | Previous workspace |
| `Alt` + `Tab` | Focus next window |
| `Shift` + `Alt` + `Tab` | Focus previous window |
| `Super` + `1` … `0` | Focus workspace 1–10 |
| `Super` + `Alt` + `1` … `0` | Send window to workspace 1–10 |
| `Ctrl` + `Super` + `Left` / `Right` | Focus workspace left/right |
| `Super` + `Shift` + `PageUp` / `PageDown` | Send window to workspace left/right |
| `Super` + `S` | Toggle scratchpad |
| `Super` + `Alt` + `S` | Send window to scratchpad |
| `Ctrl` + `Super` + `M` | Toggle music workspace |
| `Super` + arrow keys | Focus window in direction |
| `Super` + `Shift` + arrow keys | Move window in direction |
| `Super` + `Q` | Close window |
| `Super` + `D` | Maximize window |
| `Super` + `F` | Fullscreen |
| `Super` + `Alt` + `F` | Fullscreen (spoof) |
| `Super` + `Alt` + `Space` | Float/tile |
| `Super` + `Alt` + `P` | Pin window |
| `Super` + `Shift` + `Alt` + `Q` | Force-kill window |
| `Super` + left-drag | Move window |
| `Super` + right-drag | Resize window |
| `Super` + `Equal` / `Minus` | Zoom in/out |

### Utilities

| Shortcut | Action |
| --- | --- |
| `Super` + `V` | Clipboard history |
| `Super` + `.` | Emoji picker |
| `Super` + `Shift` + `S` | Screen snip |
| `Super` + `Shift` + `A` | Google Lens |
| `Super` + `Shift` + `X` | OCR to clipboard |
| `Super` + `Shift` + `T` | Translate screen content |
| `Super` + `Shift` + `C` | Pick color |
| `Super` + `Shift` + `R` | Record region (no sound) |
| `Super` + `Shift` + `Alt` + `R` | Record screen (with sound) |
| `Print` | Screenshot to clipboard |
| `Ctrl` + `Print` | Screenshot to clipboard and file |
| `Super` + `Shift` + `Alt` + right-click | AI summary of selected text |

### Media

| Shortcut | Action |
| --- | --- |
| `Super` + `Shift` + `B` | Previous track |
| `Super` + `Shift` + `N` | Next track |
| `Super` + `Shift` + `P` | Play/pause |
| `Super` + `Shift` + `M` | Mute output |
| `Super` + `Alt` + `M` | Mute microphone |
| `XF86Audio*` / `XF86MonBrightness*` | Hardware media and brightness keys |

### Apps

| Shortcut | Action |
| --- | --- |
| `Super` + `Enter` | Terminal |
| `Super` + `E` | File manager |
| `Super` + `W` | Browser |
| `Super` + `C` | Code editor |
| `Super` + `X` | Text editor |
| `Super` + `I` | Settings |
| `Ctrl` + `Super` + `V` | Volume mixer |
| `Ctrl` + `Shift` + `Esc` | Task manager |
| `Ctrl` + `Super` + `Shift` + `Alt` + `W` | Office software |

### Session

| Shortcut | Action |
| --- | --- |
| `Super` + `L` | Lock |
| `Super` + `Shift` + `L` | Sleep |
| `Ctrl` + `Alt` + `Delete` | Session menu |
| `Ctrl` + `Shift` + `Alt` + `Super` + `Delete` | Shut down |

## Memory profile

This setup is tuned for an 8 GB machine:

- Settings open inside the running shell via IPC instead of spawning a second
  Quickshell process.
- Rarely used panels are unloaded when closed.
- Resource polling and update checks are relaxed.

Native measurements on the development machine (bar-only, not a full
Quickshell replacement comparison):

| Component | PSS |
| --- | ---: |
| `dots-bar`, one 1366×768 output | ~11.3 MB |
| `dots-bar`, two outputs | ~12.5 MB |
| `dotsd`, idle | ~0.4 MB |
| `dots-osd`, visible | ~9.3 MB |
| `dots-notify`, after a popup | ~9.9 MB |

See [`docs/RAM.md`](docs/RAM.md) for the Quickshell profile and
[`docs/native/PARITY.md`](docs/native/PARITY.md) for the full measurement
notes.

## Documentation

- [`docs/native/STATUS.md`](docs/native/STATUS.md) — current native checkpoint
- [`docs/native/PARITY.md`](docs/native/PARITY.md) — feature contract and gate
- [`native/README.md`](native/README.md) — native build, tests, and limitations
- [`docs/RAM.md`](docs/RAM.md) — Quickshell memory notes
- [`docs/cbar/README.md`](docs/cbar/README.md) — early C bar prototype

## Credits and license

This repository is a fork of
[end-4/dots-hyprland](https://github.com/end-4/dots-hyprland), licensed under
the [GPL-3.0](LICENSE). Huge thanks to [@end-4](https://github.com/end-4),
[@clsty](https://github.com/clsty), and the illogical-impulse community.

The native shell also relies on
[Hyprland](https://github.com/hyprwm/hyprland),
[Quickshell](https://quickshell.outfoxxed.me/), cairo, Pango, and the
`python-validity` / `open-fprintd` projects.

This is a personal fork. For upstream installation, support, and general
questions, please use the upstream repository first. If this setup was useful,
consider starring [end-4/dots-hyprland](https://github.com/end-4/dots-hyprland)
— the upstream author did the heavy lifting.
