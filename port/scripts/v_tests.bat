@echo off
rem Verification run: Skip Intro launch options (-novid -noattractscreen), a
rem campaign started with the console bridge ("map c1m1_hotel" at 24 s),
rem l4d2_fov 110 from the start then 75 / 120 / default at 180-210 s, and the
rem real-input mouse/keyboard test at 150 s. Results in testruns\t_v\.
set "EXTRA="
call "%~dp0test_run.bat" t_v.ini 225
echo all done> "%~dp0..\testruns\v_done.txt"
