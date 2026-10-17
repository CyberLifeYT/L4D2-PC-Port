@echo off
rem Frame-rate cap tests (engine fps_max override). Results in testruns\t_fps*\.
set "EXTRA="
for %%I in (t_fps60.ini t_fps0.ini) do call "%~dp0test_run.bat" %%I 230
echo all done> "%~dp0..\testruns\fps_done.txt"
