@echo off
rem PC addon test: a PC-format texture (converted by the launcher's VTF converter)
rem and a plain-text .vmt replace the HUD health bar background. The dev
rem gameinfo gets the test mod's search path for the run and is restored after.
setlocal
set "G=%~dp0..\..\assets\left4dead2\gameinfo.xtx"
copy /y "%G%" "%G%.textest.bak" >NUL
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tex_inject.ps1" -Path "%G%"
call "%~dp0test_run.bat" t_tex.ini 120
copy /y "%G%.textest.bak" "%G%" >NUL
del "%G%.textest.bak"
echo all done> "%~dp0..\testruns\tex_done.txt"
