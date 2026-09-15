@echo off
REM Diev OS - run in QEMU from ISO
setlocal
cd /d "%~dp0"
if not exist build\diev.iso (
  echo Missing build\diev.iso - run build.bat first.
  exit /b 1
)
qemu-system-x86_64 -cdrom build\diev.iso -boot order=d -m 128M
endlocal
