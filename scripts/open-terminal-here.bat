@echo off
setlocal EnableExtensions

rem Release zip: this .bat sits next to wrec.exe
rem Dev repo:     scripts\open-terminal-here.bat  ->  ..\build\wrec.exe
set "HERE=%~dp0"
if exist "%HERE%wrec.exe" (
  cd /d "%HERE%"
  set "WREC=wrec.exe"
) else if exist "%HERE%..\build\wrec.exe" (
  cd /d "%HERE%.."
  set "WREC=build\wrec.exe"
) else (
  cd /d "%HERE%"
  set "WREC=wrec.exe"
  set "MISSING=1"
)

title wrec
echo wrec - record Windows windows to MP4
echo.
echo Folder : %CD%
echo Binary : %WREC%
if defined MISSING (
  echo Status : not found - run  .\build.ps1  from repo root
)
echo.
echo --- quick start ---
echo   %WREC% list
echo   %WREC% gui
echo   %WREC% r -t "Notepad" -o demo.mp4
echo.
echo --- multi-window ---
echo   %WREC% r -t Chrome -t Notepad --layout horizontal -o dual.mp4
echo.
echo --- other ---
echo   %WREC% install          add to %%USERPROFILE%%\.local\bin
echo   %WREC% help             full CLI reference
echo.
echo Hotkeys (default on): Ctrl+Alt+S stop/start  P pause  Q quit
echo Ctrl+C or closing the window saves before exit while recording.
echo Docs: README.md
echo.
cmd /k
