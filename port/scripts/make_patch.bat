@echo off
rem Packs the release build into dist\l4d2-port-patch-<version>.zip for a GitHub Release.
rem Usage: scripts\make_patch.bat <version> ["release notes"]
rem Needs the launcher exe (build it from ..\launcher, or download it from a release) at tools\launcher\.
setlocal
cd /d "%~dp0.."
set "VER=%~1"
if "%VER%"=="" (echo usage: make_patch.bat ^<version^> ["notes"] & exit /b 2)
set "NOTES=%~2"
if "%NOTES%"=="" set "NOTES=L4D2 PC Port %VER%"
if not exist dist mkdir dist
start "" /wait "tools\launcher\l4d2-launcher.exe" --make-patch "out\build\win-amd64-release" %VER% "dist\l4d2-port-patch-%VER%.zip" "%NOTES%"
if errorlevel 1 (echo Patch build FAILED & exit /b 1)
type "dist\l4d2-port-patch-%VER%.zip.txt"
echo.
echo Upload dist\l4d2-port-patch-%VER%.zip to a GitHub Release tagged v%VER%,
echo or run release_patch.bat / pre_release_patch.bat to build and upload in one step.
