@echo off
rem Runs the dev build against ..\assets (extracted game files next to this project).
setlocal
cd /d "%~dp0.."
set "BUILD=out\build\win-amd64-release"
if not exist logs mkdir logs
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set STAMP=%%t
"%BUILD%\l4d2.exe" --log_file "logs\l4d2_%STAMP%.log" %*
echo Exit code %ERRORLEVEL%. Log: logs\l4d2_%STAMP%.log
