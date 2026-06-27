# Architecture

## Pipeline

```text
RecorderManager
        │
        ├── CaptureSource (×N)     one WGC session per window
        ├── FrameSynchronizer      fixed-FPS tick, non-blocking
        ├── Scene                  canvas + source placements
        ├── SceneCompositor        CPU BGRA compose
        ├── cursor overlay         once on the composed frame
        └── MfEncoder              single H.264 MP4 output
```

Each stage has a narrow job. The encoder always sees one full canvas at fixed resolution; it never knows how many windows were captured.

---

## Components

### CaptureSource

One Windows Graphics Capture session per target window, with `PrintWindow` fallback when the window is occluded. Frames arrive asynchronously; each source keeps a per-source CPU BGRA buffer with the latest frame.

### FrameSynchronizer

Drives a fixed FPS clock. On each tick it takes the newest frame from every source. If a source has no new frame, the previous one is reused. The synchronizer never blocks waiting for capture.

### Scene

Defines canvas size, background, and where each source is placed. Layout math (auto, grid, horizontal, vertical, custom) lives here and does not depend on Media Foundation.

### SceneCompositor

Blits each source into the canvas with fit, fill, or stretch scaling. If a window closes mid-recording, that cell shows a **Window Closed** placeholder and recording continues.

### Cursor overlay

`GetCursorInfo` plus alpha blend, applied **once** on the composed frame. Cursor position is mapped through source placements into scene coordinates.

### MfEncoder

Receives only the final composed BGRA buffer. Source windows may resize during capture; content scales within its scene rect. The encoder is not recreated on source resize.

---

## End-to-end flow

1. **Resolve targets** — `EnumWindows` with filters; match by HWND, PID, or title substring. Multiple targets become multiple `CaptureSource` instances.
2. **Capture** — WGC + D3D11 per source while visible; `PrintWindow` when occluded.
3. **Synchronize** — Fixed FPS loop; poll hotkeys and stop requests between ticks.
4. **Compose** — CPU BGRA compositor writes one canvas-sized buffer per tick.
5. **Cursor** — Drawn on the composed buffer when enabled.
6. **Encode** — Media Foundation H.264. Timestamps follow wall clock minus paused time, scaled by `--speed`.

---

## GUI vs CLI

Both paths call the same `RecorderManager`. The CLI runs it on the main thread and polls hotkeys from a thread message queue. The GUI runs it on a worker thread; hotkeys register on the main window and actions forward through an atomic flag.

Graceful shutdown (close window, Ctrl+C, console close) sets `stopRequested`; the manager enters a stopping state, finalizes the encoder if it was opened, then exits.

---

## Limitations

- **No audio** — video only
- **Minimized windows** — may produce zero-size frames and fail
- **Unfocused apps** — some pause rendering when not visible (games, Page Visibility API)
- **Software encode path** — CPU BGRA → Media Foundation (no GPU encode pipeline yet)
- **Title matching** — each `-t` must match exactly one window
- **Custom layout** — CLI only; requires `--canvas`
- **Z-order** — source list order; last source wins for cursor hit-testing
- **H.264** — canvas width and height are rounded down to even values
- **Closed window** — placeholder in that rect; recording continues
