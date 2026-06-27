# wrec

Record a single Windows window to MP4/H.264 — from the command line or a minimal GUI.

wrec captures one top-level window (even when another window covers it), draws the mouse cursor manually, and writes H.264 via Media Foundation. No admin rights, no full-desktop capture.

## Features

- **Window-targeted capture** — pick by title, PID, or HWND; not the whole screen
- **Covered windows** — Windows Graphics Capture when visible; `PrintWindow` fallback when occluded
- **CLI + GUI** — `wrec record …` or `wrec gui`
- **Quality presets** — low → extreme; override FPS/bitrate anytime
- **Auto output names** — `wrec-YYYYMMDD-HHMMSS.mp4` when `-o` is omitted
- **Global hotkeys** — stop, pause, quit while recording
- **PATH install** — `wrec install` copies to `%USERPROFILE%\.local\bin`

## Quick start

**From a [release zip](https://github.com/mewisme/wrec/releases)** (Windows x64):

```powershell
# Extract, then from that folder:
.\wrec.exe list
.\wrec.exe gui
.\wrec.exe r -t "Notepad" -d .\captures
```

**From source** (see [Build](#build)):

```powershell
.\build.ps1
.\build\wrec.exe list
.\build\wrec.exe r -t "Notepad" -o demo.mp4
```

## Requirements

- Windows 10 **1903** (build **18362**) or later
- To build: Visual Studio 2022+ (or Build Tools), **Desktop development with C++**, Windows 10 SDK 18362+

## Install on PATH

```powershell
wrec install                    # → %USERPROFILE%\.local\bin
wrec install --dir D:\tools\bin
wrec uninstall
```

Restart your terminal after install/uninstall.

## GUI

```powershell
wrec gui
```

The window lists capturable targets (Refresh, optional **Show all**). Select a row, set output path/dir and quality, then **Start Recording**. **Stop** or hotkeys finalize the file. **Play Recent** opens the last successful recording in your default video player.

Install/uninstall controls are at the bottom of the same window.

## CLI reference

```text
wrec list|l [options]
wrec record|rec|r (-w <HWND> | -p <PID> | -t <title>) [options]
wrec gui
wrec install|uninstall [options]
wrec help
```

### List windows

```powershell
wrec list              # alias: l
wrec l -a              # include tool/invisible/shell windows
wrec l -j              # JSON to stdout
```

### Record

Pick **exactly one** target:

```powershell
wrec r -t "Notepad"                    # partial title match
wrec r -p 1234 -o out.mp4              # largest visible window for PID
wrec r -w 0x1050E                      # HWND from list
```

Output:

```powershell
wrec r -t "Notepad" -o demo.mp4        # explicit file
wrec r -t "Notepad"                    # auto: %USERPROFILE%\Videos\wrec-YYYYMMDD-HHMMSS.mp4
wrec r -t "Notepad" -d D:\captures     # auto name in folder
wrec r -t "Notepad" -o clip.mp4 -d D:\captures   # D:\captures\clip.mp4
```

#### Quality presets

Default preset is **medium**. `-f` / `-b` override preset values.

| Preset | FPS | Bitrate |
|--------|-----|---------|
| `low` | 24 | 2 Mbps |
| `medium` | 30 | 5 Mbps |
| `high` | 45 | 7 Mbps |
| `ultra` | 60 | 8 Mbps |
| `extreme` | 60 | 12 Mbps |

```powershell
wrec r -t "Notepad" --preset high
wrec r -t "Notepad" --preset ultra -f 30   # 30 fps, 8 Mbps bitrate
```

#### Record options

| Short | Long | Default | Description |
|-------|------|---------|-------------|
| `-w` | `--hwnd` | — | Target window handle |
| `-p` | `--pid` | — | Target process ID |
| `-t` | `--title` | — | Partial window title match |
| `-o` | `--out` | auto | Output MP4 path |
| `-d` | `--output-dir` | `%USERPROFILE%\Videos` | Folder for `-o` or auto-named file |
| — | `--preset` | medium | `low` … `extreme` |
| `-f` | `--fps` | preset | Frame rate (overrides preset) |
| `-b` | `--bitrate` | preset | Bitrate in bps (overrides preset) |
| `-c` | `--cursor` | on | Cursor overlay (`on` / `off`) |
| `-k` | `--hotkeys` | on | Global hotkeys (`on` / `off`) |
| `-P` | `--start-paused` | off | Arm capture; Ctrl+Alt+S to start writing |
| `-s` | `--speed` | 1 | Playback speed (`0.5`, `2x`, …) |
| `-v` | `--verbose` | off | Verbose logging |
| `-j` | `--json` | off | JSON events on stderr |
| `-a` | `--audio` | — | Not implemented (`none` only) |

### Hotkeys (when `--hotkeys on`)

| Key | Action |
|-----|--------|
| Ctrl+Alt+S | Stop and finalize (or **start** if `--start-paused`) |
| Ctrl+Alt+P | Pause / resume |
| Ctrl+Alt+Q | Quit and finalize |

Each hotkey shows a brief tray balloon notification (CLI or GUI).

### Examples

```powershell
wrec list
wrec r -t "Notepad" -o test.mp4 -f 30
wrec r -t "Notepad" -o slow.mp4 -s 0.5
wrec r -t "Notepad" -o fast.mp4 -s 2x
wrec r -t "Notepad" --start-paused -o demo.mp4
wrec r -w 0x1050E -o demo.mp4 --cursor on -v
```

## Build

Install [Ninja](https://ninja-build.org/) once (optional, matches CI):

```powershell
winget install Ninja-build.Ninja
```

```powershell
.\build.ps1                              # → build\wrec.exe
.\build.ps1 -Clean                       # clean rebuild
.\build.ps1 -Generator VisualStudio      # → build-vs\Release\wrec.exe
```

| Generator | Output |
|-----------|--------|
| Ninja (default) | `build\wrec.exe` |
| Visual Studio | `build-vs\Release\wrec.exe` |

Manual Ninja build (Developer PowerShell for VS):

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Releases

Push a tag to publish a Windows x64 zip on GitHub Releases:

```powershell
git tag v0.0.5
git push origin v0.0.5
```

Tags must start with `v`. The zip contains `wrec.exe`, README, LICENSE, and `scripts/open-terminal-here.bat`.

## How it works

1. **Target** — `EnumWindows` with filters; match by HWND, PID, or title substring.
2. **Capture** — WGC + D3D11 frame pool while the window is on top; `PrintWindow` (`PW_RENDERFULLCONTENT`) when occluded so background animation can still update.
3. **Cursor** — CPU alpha-blend from `GetCursorInfo` when the cursor is inside the target bounds.
4. **Encode** — Media Foundation H.264 sink writer. Output size is fixed from the first frame; later resizes are scaled/letterboxed.
5. **Timing** — Wall-clock timestamps (minus paused time), scaled by `--speed`.

## Limitations

- No audio
- Minimized targets may fail (zero-size frames)
- Some apps pause rendering when unfocused (games, Page Visibility API) — capture only sees what the app draws
- Software encode path (CPU BGRA → MF)
- Single window per session
- `--title` must match exactly one window
- H.264 requires even width/height (rounded down)

## Project layout

```
src/
  main.cpp                 Entry, DPI, WinRT apartment
  app/                     CLI, GUI, install, record options
    cli.cpp                Argument parsing, command dispatch
    gui.cpp                Win32 GUI
    record_options.cpp     Presets, output path resolution
    path_install.cpp       install / uninstall
  core/                    Shared utilities
    result.h               Result<T>, Status
    logging.cpp            Logging, UTF-8 console
    notification.cpp       Tray balloon on hotkey actions
  capture/                 Window discovery and frame capture
    window_list.cpp        EnumWindows, target resolution
    capture_wgc.cpp        Windows Graphics Capture
    capture_printwindow.cpp   Occluded-window fallback
    d3d_device.cpp         D3D11 staging, scale/copy
    cursor_overlay.cpp     Cursor alpha-blend
  record/                  Encode session
    recorder.cpp           Session orchestration
    hotkeys.cpp            Ctrl+Alt+S/P/Q
    mf_encoder.cpp         H.264 MP4 encoder
```

## License

MIT — Copyright (c) 2026 Mew. See [LICENSE](LICENSE).
