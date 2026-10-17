@echo off
rem PC controls test: real SendInput mouse look / W / LMB / R / F after 150 s,
rem FOV commands through the console bridge at 180-210 s, client image dump at
rem 120 s. Results in testruns\t_pc\ (plus logs\image_client_88000000.bin).
set "EXTRA="
set "L4D2_DUMP_IMAGES=1"
call "%~dp0test_run.bat" t_pc.ini 225
echo all done> "%~dp0..\testruns\pc_done.txt"
