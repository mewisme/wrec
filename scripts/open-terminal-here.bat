@echo off
cd /d "%~dp0"
title wrec
echo wrec - extracted folder: %CD%
echo.
echo Examples:
echo   wrec.exe l
echo   wrec.exe r -t "Notepad" -o demo.mp4 -f 30
echo.
cmd /k
