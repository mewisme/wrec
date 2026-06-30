# wrec

Record one or more Windows windows into a single MP4 — from the command line or a small GUI.

wrec captures **top-level windows** (not the full desktop), composes them into one scene, draws the cursor once on the final frame, and encodes H.264 with Media Foundation. No admin rights required.

## Features

- **Window-targeted capture** — select by title, PID, or HWND
- **Multi-window scenes** — grid, horizontal, vertical, focus, or custom layouts in one MP4
- **Covered windows** — Windows Graphics Capture when visible; `PrintWindow` when occluded
- **CLI and GUI** — run `wrec` for the GUI, or script with `wrec record`
- **Quality presets** — low through extreme; override FPS and bitrate anytime
- **Global hotkeys** — stop, pause, and quit while recording
- **PATH install** — `wrec install` copies to `%USERPROFILE%\.local\bin` and adds desktop + Start menu shortcuts named **wrec**

## Installation

Windows x64 only.

### Scoop

Add this repo as a bucket (one-time), then install:

```powershell
scoop bucket add mew https://github.com/mewisme/wrec
scoop install wrec
```

Install adds **wrec** shortcuts on the desktop and in the Start menu.

Updates:

```powershell
scoop update
scoop update wrec
```

Uninstall:

```powershell
scoop uninstall wrec
```

Removes the app, shim, and **wrec** shortcuts. To drop the bucket: `scoop bucket rm mew`.

### PowerShell

Latest release:

```powershell
irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1 | iex
```

From **cmd**:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1 | iex"
```

Specific version:

```powershell
& ([ScriptBlock]::Create((irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1))) -Version v1.2.3
```

Or, with an environment variable:

```powershell
$env:WREC_VERSION = 'v1.2.3'; irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1 | iex
```

The PowerShell installer downloads from [GitHub Releases](https://github.com/mewisme/wrec/releases) and runs `wrec install`.

Uninstall:

```powershell
wrec uninstall
```

Removes `%USERPROFILE%\.local\bin\wrec.exe`, the PATH entry, and **wrec** shortcuts. Restart your terminal afterward.

### Manual

1. Download the latest [release ZIP](https://github.com/mewisme/wrec/releases) (`wrec-v<version>-windows-amd64.zip`).
2. Extract `wrec.exe`.
3. Install:

```powershell
.\wrec.exe install
```

Check version and how it was installed:

```powershell
wrec -V
```

`Installed via` is detected from the running executable path (e.g. `Scoop`, `manual PATH install`, `portable ZIP`).

Uninstall:

```powershell
wrec uninstall
wrec uninstall -d D:\tools\bin   # if you used a custom --dir
```

Or use **Uninstall** in the GUI. Restart your terminal afterward.

### Portable (no install)

If you only extracted the ZIP and never ran `wrec install`:

1. Quit wrec (**Exit** from the tray menu if the GUI is running).
2. Delete the folder containing `wrec.exe`.
3. If you opened the GUI once, remove **wrec** shortcuts from the desktop and Start menu (`wrec.lnk`).

No PATH entry is added for portable use.

## Quick start

**Release** ([download zip](https://github.com/mewisme/wrec/releases)) — Windows x64:

```powershell
.\wrec.exe list
.\wrec.exe
.\wrec.exe gui
.\wrec.exe r -t "Notepad" -d .\captures
```

Running `wrec` with no command opens the GUI. Double-clicking `wrec.exe` shows the GUI only.

**From source** — see [docs/build.md](docs/build.md):

```powershell
.\build.ps1
.\build\wrec.exe r -t "Notepad" -o demo.mp4
```

## Requirements

- Windows 10 **1903** (build **18362**) or later
- To build: Visual Studio 2022+ with **Desktop development with C++** and Windows 10 SDK 18362+

## Documentation

| Doc | Contents |
|-----|----------|
| [Guide](docs/guide.md) | Install on PATH, GUI walkthrough, hotkeys, graceful exit |
| [CLI reference](docs/cli.md) | Commands, options, layouts, presets, examples |
| [Architecture](docs/architecture.md) | Pipeline, capture model, limitations |
| [Build](docs/build.md) | Build from source, project layout |

## License

MIT — Copyright (c) 2026 Mew. See [LICENSE](LICENSE).
