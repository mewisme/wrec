# Build

## Prerequisites

- Windows 10 SDK **18362** or later
- Visual Studio 2022+ (or Build Tools) with **Desktop development with C++**
- [Ninja](https://ninja-build.org/) (optional; matches CI)

Install Ninja once:

```powershell
winget install Ninja-build.Ninja
```

## Build commands

From the repo root:

```powershell
.\build.ps1                              # Release → build\wrec.exe
.\build.ps1 -Clean                       # clean rebuild
.\build.ps1 -Generator VisualStudio      # → build-vs\Release\wrec.exe
```

Default generator is **Ninja** + **Release**, same as CI.

## Project layout

```text
src/
  main.cpp                 Entry, DPI, WinRT apartment
  app/                     CLI, GUI, install, record options
  core/                    Result, logging, notifications
  capture/                 Window discovery, WGC, PrintWindow, CaptureSource
  scene/                   Scene model, compositor, layouts
  record/                  RecorderManager, FrameSynchronizer, encoder, hotkeys
```

## CI

GitHub Actions workflows under `.github/workflows/` build on push and produce release zips. See `build.yml` and `release.yml` for details.
