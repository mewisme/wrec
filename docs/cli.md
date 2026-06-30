# CLI reference

## Commands

```text
wrec [gui]              Open GUI (default when no subcommand)
wrec list|l [options]
wrec record|rec|r (-w <HWND> | -p <PID> | -t <title> | -S <spec>) [options]
wrec gui
wrec install|uninstall [options]
wrec -V, --version
wrec help
```

### Version

```powershell
wrec -V
wrec --version
```

Shows the ASCII banner, version, license, and install source (`Scoop`, `manual PATH install`, `portable ZIP`).

`wrec install` and `wrec uninstall` are for manual ZIP / PowerShell installs only (not Scoop).

```powershell
wrec install [-d <dir>] [-v]
wrec uninstall [-d <dir>] [-v]
```

Default install directory: `%USERPROFILE%\.local\bin`. `-d` must match the directory used at install time.

| Installed via | Uninstall |
|---------------|-----------|
| Scoop | `scoop uninstall wrec` |
| PowerShell script / `wrec install` | `wrec uninstall` (or GUI **Uninstall**) |
| Portable ZIP (never installed) | Delete `wrec.exe` and remove `wrec.lnk` shortcuts if created |

Restart your terminal after `wrec uninstall` so PATH changes take effect.

---

## List windows

```powershell
wrec list              # alias: l
wrec l -a              # include tool/invisible/shell windows
wrec l -j              # JSON to stdout
wrec l -v              # verbose
```

Use the HWND column from `wrec list` with `-w` / `--hwnd`.

---

## Record

At least one target is required: `-w`, `-p`, `-t`, or `-S` / `--source`. Flags can be repeated for multiple windows.

### Single window

```powershell
wrec r -t "Notepad"                    # partial title match
wrec r -p 1234 -o out.mp4              # largest visible window for PID
wrec r -w 0x1050E                      # HWND from list
```

### Multiple windows

```powershell
wrec r -t Chrome -t "Visual Studio Code" -o session.mp4
wrec r -p 1234 -p 4567 --layout horizontal -o dual.mp4
wrec r -w 0x123456 -w 0xABCDEF --layout grid -o grid.mp4
wrec r -t Chrome -t Notepad --layout focus -o focus.mp4
```

### Custom layout

Requires `--canvas` and one or more `-S` / `--source` entries:

```powershell
wrec r --canvas 1920x1080 `
  -S hwnd=0x123456,x=0,y=0,w=960,h=540,scale=fit `
  -S hwnd=0xABCDEF,x=960,y=0,w=960,h=540,scale=fit `
  -o custom.mp4
```

Scale modes: `fit` (letterbox), `fill` (crop), `stretch`.

### Output paths

```powershell
wrec r -t "Notepad" -o demo.mp4        # explicit file
wrec r -t "Notepad"                    # auto name in Videos folder
wrec r -t "Notepad" -d D:\captures     # auto name in folder
```

---

## Layouts

| Mode | Behavior |
|------|----------|
| `auto` | One source: canvas matches first frame. Multiple: horizontal for 2–3 windows, grid otherwise |
| `grid` | Even grid; cell size from largest source (or even split when `--canvas` is set) |
| `horizontal` | Side by side |
| `vertical` | Stacked |
| `focus` | One target at a time; switches when a selected target becomes the foreground window |
| `custom` | Explicit `--source` placements; **requires `--canvas`** |

---

## Quality presets

Default preset is **medium**. `--fps` and `--bitrate` override preset values.

| Preset | FPS | Bitrate |
|--------|-----|---------|
| `low` | 24 | 2 Mbps |
| `medium` | 30 | 5 Mbps |
| `high` | 45 | 7 Mbps |
| `ultra` | 60 | 8 Mbps |
| `extreme` | 60 | 12 Mbps |

---

## Record options

Short flags are only available for targets, output paths, verbose, and custom source specs. Everything else is long-form only.

| Short | Long | Default | Description |
|-------|------|---------|-------------|
| `-w` | `--hwnd` | — | Target window handle (repeatable) |
| `-p` | `--pid` | — | Target process ID (repeatable) |
| `-t` | `--title` | — | Partial title match (repeatable; each `-t` must match exactly one window) |
| `-o` | `--out` | auto | Output MP4 path |
| `-d` | `--output-dir` | `%USERPROFILE%\Videos` | Folder for `-o` or auto-named file |
| `-v` | `--verbose` | off | Verbose logging |
| `-S` | `--source` | — | Custom placement spec (repeatable) |
| — | `--layout` | auto | `auto`, `grid`, `horizontal`, `vertical`, `focus`, `custom` |
| — | `--canvas` | auto | Canvas size `WxH` (required for custom layout) |
| — | `--preset` | medium | `low` … `extreme` |
| — | `--fps` | preset | Frame rate |
| — | `--bitrate` | preset | Bitrate in bps |
| — | `--compress` | off | Post-record re-encode: `off`, `small`, `medium`, `aggressive` (requires ffmpeg in PATH) |
| — | `--cursor` | on | Cursor on composed frame (`on` / `off`) |
| — | `--hotkeys` | on | Global hotkeys (`on` / `off`) |
| — | `--start-paused` | off | Arm capture; Ctrl+Alt+S to start writing |
| — | `--speed` | 1 | Playback speed multiplier (`0.5`, `2x`, …) |
| — | `--json` | off | JSON events on stderr |
| — | `--audio` | — | Not implemented (`none` only) |

---

## Post-recording compression

After the MP4 is finalized, `--compress` can re-encode the file with ffmpeg (must be on `PATH`). Recording still succeeds if ffmpeg is missing or compression fails; the original file is kept.

| Level | Target video bitrate |
|-------|----------------------|
| `off` | No post-compression (default) |
| `small` | 70% of recording bitrate |
| `medium` | 45% of recording bitrate |
| `aggressive` | 25% of recording bitrate |

```powershell
wrec r -t "Notepad" --compress small
wrec r -t "Notepad" --compress medium --bitrate 8000000
```

Compression runs only after successful finalize. Audio is stripped in the compressed output (`-an`).

---

## Hotkeys

When `--hotkeys on` (default). See [guide.md](guide.md#hotkeys) for the key map.

---

## Interrupt handling

While recording from the CLI, **Ctrl+C**, **Ctrl+Break**, or closing the console requests a graceful stop: wrec finalizes the MP4 before exiting. You will see `Stopping and saving…` in the log.
