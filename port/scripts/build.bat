@echo off
rem Incremental release build. Pass "full" to re-run codegen first (after editing l4d2_manifest.toml).
setlocal
cd /d "%~dp0.."
call scripts\vsenv.bat || exit /b 1
if not exist logs mkdir logs
if /i "%~1"=="full" (
  cmake --build --preset win-amd64-release --target l4d2_codegen || exit /b 1
  cmake -DPROJ=. -P fix_vscript_table.cmake || exit /b 1
)
rem Idempotent: keeps the generated headers' indirect-call helpers current.
cmake -DPROJ=. -P fix_vscript_table.cmake || exit /b 1
cmake --build --preset win-amd64-release -j %NUMBER_OF_PROCESSORS%
