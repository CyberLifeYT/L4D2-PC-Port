@echo off
rem Stick-deadzone calibration: from 140 s the right stick is held at fixed
rem levels (0.16 ... 0.60) for 3 s each; autoshots every second show which
rem levels turn the view. Results in testruns\t_cal\.
set "EXTRA="
call "%~dp0test_run.bat" t_cal.ini 200
echo all done> "%~dp0..\testruns\cal_done.txt"
