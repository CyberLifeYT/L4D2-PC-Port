@echo off
rem Feature tests with the autopilot (presses START/A to reach Dead Center). Results in testruns\t_*\.
set "EXTRA="
for %%I in (t_raw.ini t_fps60.ini t_wide.ini) do call "%~dp0test_run.bat" %%I 140
echo all done> "%~dp0..\testruns\features_done.txt"
