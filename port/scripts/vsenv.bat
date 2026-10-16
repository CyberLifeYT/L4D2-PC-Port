@echo off
rem Sets up the MSVC x64 + clang + ninja environment (Visual Studio 2022+/18 with C++ and clang tools).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio not found & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >NUL
set "PATH=%ProgramFiles%\LLVM\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
