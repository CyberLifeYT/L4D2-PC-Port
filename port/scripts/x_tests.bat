@echo off
rem Combined check: 32:9, starter-mod commands, PC key prompts, chapter unlock (menu), profiler.
rem Results in testruns\t_x\ (profile.txt is copied there too).
call "%~dp0test_run.bat" t_x.ini 200
copy /y "%~dp0..\out\build\win-amd64-release\logs\profile.txt" "%~dp0..\testruns\t_x\" >NUL 2>&1
copy /y "%~dp0..\out\build\win-amd64-release\logs\paths.txt" "%~dp0..\testruns\t_x\" >NUL 2>&1
echo all done> "%~dp0..\testruns\x_done.txt"
