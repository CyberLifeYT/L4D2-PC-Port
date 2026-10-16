@echo off
rem Shared by release_patch.bat / pre_release_patch.bat. Do not run directly.
setlocal
set "KIND=%~1"
set "VER=%~2"
set "NOTES=%~3"
if "%VER%"=="" (
  echo usage: %KIND%_patch.bat ^<version^> ["notes"]
  exit /b 2
)
call "%~dp0make_patch.bat" %VER% "%NOTES%" || exit /b 1
cd /d "%~dp0.."
set "L=tools\launcher\l4d2-launcher.exe"
set "FLAGS="
if /i "%KIND%"=="prerelease" set "FLAGS=--prerelease"
if "%NOTES%"=="" set "NOTES=L4D2 PC Port %VER%"
echo.
echo Publishing v%VER% (%KIND%) to GitHub...
start "" /wait "%L%" --publish "dist\l4d2-port-patch-%VER%.zip" %VER% %FLAGS% --notes "%NOTES%" --file "%L%" --log "dist\publish-%VER%.txt"
type "dist\publish-%VER%.txt"
findstr /b /c:"OK:" "dist\publish-%VER%.txt" >NUL || (echo. & echo Publishing FAILED - the zip is still in dist\ to upload by hand. & exit /b 1)
