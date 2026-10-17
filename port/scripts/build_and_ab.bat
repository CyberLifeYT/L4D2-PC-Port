@echo off
call "%~dp0build.bat" > "%~dp0..\logs\build.log" 2>&1
if errorlevel 1 (echo build failed> "%~dp0..\testruns\ab_done.txt" & exit /b 1)
call "%~dp0ab_tests.bat"
