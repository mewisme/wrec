# wrec

Windows CLI screen recorder. Captures a specific top-level window using **Windows Graphics Capture** (works when the window is covered), overlays the mouse cursor manually, and encodes to **MP4/H.264** via Media Foundation.

## License

MIT — Copyright (c) 2026 Mew. See [LICENSE](LICENSE).

## Requirements

- Windows 10 version **1903** (build **18362**) or later
- Visual Studio 2022 with **Desktop development with C++**
- Windows 10 SDK (10.0.18362+)

## Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Binary: `build\Release\wrec.exe`

### Install to PATH

```powershell
# Copy to %USERPROFILE%\.local\bin and add to user PATH
.\build\Release\wrec.exe install

# Custom directory
wrec install --dir D:\tools\bin

# Remove from PATH and delete installed copy
wrec uninstall
```

Restart your terminal after install/uninstall.

### Releases

Prebuilt Windows x64 binaries are attached to GitHub Releases when a version tag is pushed:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

Tag names must start with `v` (e.g. `v1.0.0`, `v0.2.1`). The release workflow builds `wrec.exe` and publishes `wrec-<version>-windows-x64.zip`.

## Usage

### List windows

```powershell
wrec list          # alias: l
wrec l -a          # --all
wrec l -j          # --json
```

### Record a window

```powershell
wrec record --hwnd 0x12345 --out capture.mp4   # aliases: rec, r
wrec r -p 1234 -o capture.mp4
wrec r -t "Notepad" -o capture.mp4 -f 30
```

Common options (short and long forms):

| Short | Long | Default | Description |
|-------|------|---------|-------------|
| `-w` | `--hwnd` | — | Target window handle |
| `-p` | `--pid` | — | Target process ID |
| `-t` | `--title` | — | Partial window title match |
| `-o` | `--out` | — | Output MP4 path (required) |
| `-f` | `--fps` | 60 | Output frame rate |
| `-b` | `--bitrate` | 8000000 | H.264 bitrate (bps) |
| `-c` | `--cursor` | on | Draw cursor overlay (`on\|off`) |
| `-a` | `--audio` | — | Audio not implemented (`none` only) |
| `-k` | `--hotkeys` | on | Global hotkeys (`on\|off`) |
| `-P` | `--start-paused` | off | Arm capture; Ctrl+Alt+S to start |
| `-s` | `--speed` | 1 | Playback speed (`0.9`, `2x`, etc.) |
| `-v` | `--verbose` | off | Verbose logging |
| `-j` | `--json` | off | JSON events on stderr |

### Hotkeys

| Key | Default behavior |
|-----|------------------|
| **Ctrl+Alt+S** | **Stop** and finalize (or **Start** if `--start-paused`) |
| **Ctrl+Alt+P** | Pause / resume |
| **Ctrl+Alt+Q** | Quit and finalize |

### Examples

```powershell
# Find Notepad
wrec list

# Record Notepad at 30 fps
wrec record --title "Notepad" --out test.mp4 --fps 30

# Record with cursor, verbose logging
wrec record --hwnd 0x1050E --out demo.mp4 --cursor on --verbose

# Record at half speed (slow motion)
wrec r -t "Notepad" -o slow.mp4 -f 30 -s 0.5

# Record at 2x speed
wrec r -t "Notepad" -o fast.mp4 -s 2x

# Arm first, start with hotkey
wrec record --title "Notepad" --out demo.mp4 --start-paused
```

## How it works

1. **Window pick** — `EnumWindows` + filters (`list`), or match by HWND/PID/title.
2. **Capture** — `IGraphicsCaptureItemInterop::CreateForWindow` + D3D11 frame pool. WGC cursor capture is disabled; cursor is drawn manually.
3. **Cursor** — `GetCursorInfo` / `GetIconInfo`, CPU alpha-blend into the BGRA frame. Cursor is drawn when its screen position falls inside the target window bounds (default). Covered-window cursor is still shown because overlay uses global cursor position, not z-order.
4. **Encode** — Media Foundation Sink Writer, H.264 MP4. Output resolution is fixed from the first frame; later resizes are letterboxed into that size.
5. **Pause** — Frames are still captured but not written; timestamps follow wall clock (minus paused time), scaled by `--speed`.

## Known limitations

- **No audio** — `--audio none` only; code is structured for future `IAudioClient` integration.
- **Minimized windows** — may produce zero-size frames; recording stops with a message.
- **CPU encode path** — BGRA copied to MF in software (no GPU MF DXGI path yet).
- **Fixed output size** — set at session start; resize scales/letterboxes into that buffer.
- **Cursor outside target** — not drawn when outside the target window screen bounds (default `Hide` policy).
- **Single window** — one capture target per process.
- **Ambiguous `--title`** — errors if zero or multiple matches.
- **H.264 dimensions** — width/height rounded down to even values.

## Acceptance checklist

1. `wrec list` shows common apps (Notepad, Chrome, VS Code).
2. `wrec record --title "Notepad" --out test.mp4 --fps 30` creates a playable MP4.
3. Covered target still shows the target content (WGC, not BitBlt).
4. Cursor visible inside target bounds with `--cursor on`.
5. Ctrl+Alt+P pause/resume without corrupting the file.
6. Ctrl+Alt+Q exits and finalizes.
7. Target window close exits gracefully.
8. Unsupported OS prints a clear error.
9. Resize during recording does not crash (letterbox into fixed size).
10. Cursor over covered target area appears in output.
11. Cursor outside target bounds is not drawn (default).

## Project layout

```
src/
  main.cpp           Entry, DPI, WinRT apartment
  cli.cpp            Argument parsing
  window_list.cpp    EnumWindows + target resolution
  capture_wgc.cpp    Windows Graphics Capture
  d3d_device.cpp     D3D11 staging + scale/copy
  cursor_overlay.cpp Cursor alpha-blend
  mf_encoder.cpp     MF Sink Writer H.264
  hotkeys.cpp        Ctrl+Alt+S/P/Q
  recorder.cpp       Session orchestration
  logging.cpp        Logging + HRESULT helpers
  result.h           Result<T> / Status
```
