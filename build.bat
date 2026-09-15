@echo off
REM Diev OS - build script (Windows, Ninja + CMake)
setlocal
cd /d "%~dp0"
cmake -B build -G Ninja
if errorlevel 1 exit /b 1
cmake --build build
if errorlevel 1 exit /b 1
echo.
echo Build OK: build\diev.img
endlocal
