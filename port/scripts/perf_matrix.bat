@echo off
rem Runs Dead Center with each perf_*.ini for 150 s; results in testruns\perf_*\.
for %%I in (perf_full.ini perf_some.ini perf_none.ini perf_noocc.ini) do call "%~dp0test_map.bat" %%I 150
echo all done> "%~dp0..\testruns\matrix_done.txt"
