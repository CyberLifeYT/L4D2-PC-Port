@echo off
rem Incremental build, then the Dead Center test (results in testruns\).
if not exist "%~dp0..\logs" mkdir "%~dp0..\logs"
call "%~dp0build.bat" > "%~dp0..\logs\build.log" 2>&1
if errorlevel 1 (echo build failed - see logs\build.log & exit /b 1)
call "%~dp0test_map.bat" %*
