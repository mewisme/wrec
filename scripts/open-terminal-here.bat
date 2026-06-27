@echo off
cd /d "%~dp0"
title wrec
echo wrec - folder: %CD%
echo.
echo Examples:
echo   wrec.exe list
echo   wrec.exe gui
echo   wrec.exe r -t "Notepad" -d .\captures
echo   wrec.exe install
echo.
cmd /k
