@echo off
REM EOS OS - run in QEMU from ISO
setlocal
cd /d "%~dp0"
if not exist build\eos.iso (
  echo Missing build\eos.iso - run build.bat first.
  exit /b 1
)
qemu-system-x86_64 -cdrom build\eos.iso -boot order=d -m 128M
endlocal
