# User guide

## Install on PATH

```powershell
wrec install                    # → %USERPROFILE%\.local\bin
wrec install --dir D:\tools\bin
wrec uninstall
```

Manual ZIP / PowerShell installs only — not available when wrec was installed via winget or scoop.

Restart your terminal after install or uninstall so PATH changes take effect.

The GUI also has **Install** / **Uninstall** controls at the bottom of the window.

---

## GUI

Launch:

```powershell
wrec
wrec gui
```

Both open the same GUI. With no subcommand, `wrec` defaults to the GUI.

### Workflow

1. **Refresh** the window list. Enable **Show all** to include tool, invisible, or shell windows.
2. **Check** one or more rows to record (multi-select).
3. Choose a **Layout** (auto, grid, horizontal, vertical).
4. Set output file or folder, quality preset, FPS, and bitrate as needed.
5. Click **Start Recording**.

**Cursor** and **Hotkeys** are checked by default. **Start paused** arms capture without writing frames until you press Ctrl+Alt+S.

**Stop** (or Ctrl+Alt+S while recording) finalizes the MP4. **Play Recent** opens the last successful recording in your default player.

Custom per-source placement (`--source` / `--canvas`) is CLI-only; the GUI uses automatic layouts.

### System tray

When the GUI is running, a tray icon stays in the notification area.

- **Close the window** — hides the GUI to the tray; recording and hotkeys keep working.
- **First hide** — shows a one-time balloon: *wrec is still running in the system tray.*
- **Left-click the tray icon** — shows the GUI (or brings it to the foreground if already visible).
- **Right-click** — context menu:
  - Show GUI / Hide GUI
  - Show Console / Hide Console (if a console is attached)
  - Start Recording / Stop Recording
  - Pause / Resume (while recording)
  - Open Output Folder
  - Exit — stops recording gracefully if active, then quits

To fully exit, use **Exit** from the tray menu (or stop recording first, then exit). Closing the window does not quit the app.

### Closing while recording

Closing the GUI window hides it to the tray; recording continues. Use the tray **Exit** item (or Ctrl+Alt+Q) to stop, finalize the MP4, and quit.

If you close the **console** while recording, wrec stops gracefully: it finalizes the file first, then exits. The status bar shows **Stopping and saving…** during this.

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
