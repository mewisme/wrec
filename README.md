# wrec

Record one or more Windows windows into a single MP4 — from the command line or a small GUI.

wrec captures **top-level windows** (not the full desktop), composes them into one scene, draws the cursor once on the final frame, and encodes H.264 with Media Foundation. No admin rights required.

## Features

- **Window-targeted capture** — select by title, PID, or HWND
- **Multi-window scenes** — grid, horizontal, vertical, or custom layouts in one MP4
- **Covered windows** — Windows Graphics Capture when visible; `PrintWindow` when occluded
- **CLI and GUI** — scriptable `wrec record` or `wrec gui` with multi-select
- **Quality presets** — low through extreme; override FPS and bitrate anytime
- **Global hotkeys** — stop, pause, and quit while recording
- **PATH install** — `wrec install` copies to `%USERPROFILE%\.local\bin`

## Quick start

**Release** ([download zip](https://github.com/mewisme/wrec/releases)) — Windows x64:

```powershell
.\wrec.exe list
.\wrec.exe gui
.\wrec.exe r -t "Notepad" -d .\captures
```

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
