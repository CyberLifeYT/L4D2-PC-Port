@echo off
rem Automated render test: test_run.bat <settings ini in the build dir> <seconds>
rem Runs the game windowed with the given settings, saves a screenshot every few
rem seconds (l4d2_autoshot in the ini), closes it, and collects screenshots,
rem perf_summary.csv and the log into testruns\<ini name>\.
rem Optional: set EXTRA=extra game arguments, e.g. set "EXTRA=--cl="+map c1m1_hotel""
setlocal
set "ROOT=%~dp0.."
cd /d "%ROOT%\out\build\win-amd64-release" || exit /b 1
set "INI=%~1"
if "%INI%"=="" set "INI=test_rov.ini"
set "NAME=%INI:~0,-4%"
set "SECS=%~2"
if "%SECS%"=="" set "SECS=60"
if exist "%ROOT%\scripts\%INI%" copy /y "%ROOT%\scripts\%INI%" . >NUL
if exist logs\shots rmdir /s /q logs\shots
del /q logs\perf_summary.csv 2>NUL
start "" l4d2.exe --l4d2_settings_file="%CD%\%INI%" --log_file "logs\test_%NAME%.log" %EXTRA%
timeout /t %SECS% /nobreak >NUL
taskkill /im l4d2.exe /f >NUL 2>&1
timeout /t 2 /nobreak >NUL
set "OUT=%ROOT%\testruns\%NAME%"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
move /y logs\shots\*.bmp "%OUT%\" >NUL 2>&1
copy /y logs\perf_summary.csv "%OUT%\" >NUL 2>&1
copy /y "logs\test_%NAME%.log" "%OUT%\" >NUL 2>&1
copy /y logs\settings_applied.txt "%OUT%\" >NUL 2>&1
echo done> "%OUT%\done.txt"
