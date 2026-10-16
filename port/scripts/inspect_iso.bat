@echo off
rem Checks that an ISO is a valid Left 4 Dead 2 Xbox 360 disc. Usage: scripts\inspect_iso.bat "path\to\game.iso"
setlocal
cd /d "%~dp0.."
if "%~1"=="" (echo usage: inspect_iso.bat "game.iso" & exit /b 2)
if not exist dist mkdir dist
start "" /wait "tools\launcher\l4d2-launcher.exe" --inspect-iso "%~1" "dist\iso_report.txt"
type dist\iso_report.txt
