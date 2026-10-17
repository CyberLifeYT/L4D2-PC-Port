@echo off
rem A/B frame-rate test of the indirect-call cache: the same 150 s run twice,
rem with the cache (testruns\t_ab_on) and without (testruns\t_ab_off), then the
rem chapter-unlock menu check (testruns\t_ch) and the PC texture test.
setlocal
set "L4D2_NO_INDIRECT_CACHE="
call "%~dp0test_run.bat" t_ab.ini 150
if exist "%~dp0..\testruns\t_ab_on" rmdir /s /q "%~dp0..\testruns\t_ab_on"
move "%~dp0..\testruns\t_ab" "%~dp0..\testruns\t_ab_on" >NUL
set "L4D2_NO_INDIRECT_CACHE=1"
call "%~dp0test_run.bat" t_ab.ini 150
if exist "%~dp0..\testruns\t_ab_off" rmdir /s /q "%~dp0..\testruns\t_ab_off"
move "%~dp0..\testruns\t_ab" "%~dp0..\testruns\t_ab_off" >NUL
set "L4D2_NO_INDIRECT_CACHE="
call "%~dp0test_run.bat" t_ch.ini 70
call "%~dp0tex_tests.bat"
echo all done> "%~dp0..\testruns\ab_done.txt"
