@echo off
rem 3-minute test that boots straight into Dead Center (c1m1_hotel).
rem Usage: test_map.bat [settings ini] [seconds]
set "EXTRA=--cl="+map c1m1_hotel""
set "I=%~1"
if "%I%"=="" set "I=test_rov.ini"
set "T=%~2"
if "%T%"=="" set "T=180"
call "%~dp0test_run.bat" %I% %T%
