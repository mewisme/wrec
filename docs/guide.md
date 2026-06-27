# User guide

## Install on PATH

```powershell
wrec install                    # → %USERPROFILE%\.local\bin
wrec install --dir D:\tools\bin
wrec uninstall
```

Restart your terminal after install or uninstall so PATH changes take effect.

The GUI also has **Install** / **Uninstall** controls at the bottom of the window.

---

## GUI

Launch:

```powershell
wrec gui
```

### Workflow

1. **Refresh** the window list. Enable **Show all** to include tool, invisible, or shell windows.
2. **Check** one or more rows to record (multi-select).
3. Choose a **Layout** (auto, grid, horizontal, vertical).
4. Set output file or folder, quality preset, FPS, and bitrate as needed.
5. Click **Start Recording**.

**Cursor** and **Hotkeys** are checked by default. **Start paused** arms capture without writing frames until you press Ctrl+Alt+S.

**Stop** (or Ctrl+Alt+S while recording) finalizes the MP4. **Play Recent** opens the last successful recording in your default player.

Custom per-source placement (`--source` / `--canvas`) is CLI-only; the GUI uses automatic layouts.

### Closing while recording

If you close the window or press **Ctrl+C** in the terminal that launched the GUI while a session is active, wrec stops gracefully: it finalizes the file first, then exits. The status bar shows **Stopping and saving…** during this.

---

## Hotkeys

Enabled by default in the GUI and with `--hotkeys on` (default) in the CLI.

| Key | Action |
|-----|--------|
| Ctrl+Alt+S | Stop and finalize — or **start** writing if armed (`--start-paused` / **Start paused**) |
| Ctrl+Alt+P | Pause / resume |
| Ctrl+Alt+Q | Quit and finalize |

Each action shows a brief tray balloon notification.

In the GUI, hotkeys are registered on the main window and forwarded to the recording thread.

---

## Output files

| Situation | Result |
|-----------|--------|
| `-o demo.mp4` | Writes to that path (relative paths resolve against the output folder) |
| No `-o` | Auto name: `%USERPROFILE%\Videos\wrec-YYYYMMDD-HHMMSS.mp4` |
| `-d D:\captures` only | Same auto name inside `D:\captures` |

Default output folder is **Videos** under your user profile.
