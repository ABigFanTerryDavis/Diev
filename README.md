# EOS — a tiny hobby operating system

EOS (formerly *Diev*) is a 32-bit hobby OS for classic BIOS PCs, written in C
and x86 assembly. It boots from CD into a shell with an in-RAM filesystem, a
text-mode file browser and editor, a PS/2 mouse with draggable windows, and
even its own assembler.

## Features (v0.0.8)

- **Bootable CD** — El Torito no-emulation image, built with pure Python
  (no xorriso needed).
- **Shell** (`eos>`) — `help ver clear echo beep reboot mem iso time`,
  `exec` for flat `EOS!` programs, `crash` (fault-handler demo), and more.
- **RMFS** — tiny in-RAM filesystem (`ls cat write rm touch cp wc mkdir`).
- **div** — text-mode file browser, viewer, and editor (arrows, Enter,
  Shift+Enter to edit, F2…).
- **GUI** — PS/2 mouse, draggable overlapping windows with save/restore,
  and **EWord**, a 160-word dictionary lookup window.
- **Keyboard** — US and Turkish-Q layouts (`kb us|tr`).
- **EOSCWA** — EOS's own assembler; the build proves it byte-identical to
  NASM on every bootloader/object input.

## Quick start

You need QEMU to run, plus GCC (MinGW), CMake, Ninja, NASM, and Python 3
to build:

```
build.bat
run.bat
```

`build.bat` configures with CMake and builds `build/eos.iso` (bootable CD).
`run.bat` boots it in QEMU:

```
qemu-system-x86_64 -cdrom build/eos.iso -boot order=d -m 128M
```

Type `help` at the `eos>` prompt. `div` opens the file browser;
`eword` opens the dictionary window (needs a mouse — move it, drag windows
by their title bars, Esc closes).

## VirtualBox

No toolchain needed: create a VM (type Other/Unknown, 128 MB RAM, no hard
disk, EFI off), attach `eos.iso` from the main folder as the CD, and boot.
"VirtualBox" is a trademark of Oracle; this project is not affiliated with
or endorsed by Oracle.

## Shell cheat sheet

```
help            list commands            ls [dir]        list files
ver             show version             cat <file>      print a file
clear           clear screen             write <f> <t>   write text to a file
div [dir]       browse / view / edit     rm <file>       delete a file
kb [us|tr]      keyboard layout          touch <file>    create empty file
exec <file>     run an EOS program       cp <src> <dst>  copy a file
time            CMOS clock               wc <file>       count lines and bytes
mem             memory + FS usage        iso             boot CD info
beep [freq]     PC speaker               echo <text>     print text
reboot          reboot                   eword           dictionary window
crash           trigger a fault (test)
```

## Project layout

```
boot/       boot.asm (512-byte bootloader, loads the kernel)
kernel/     C sources: shell, RMFS, IDT, div, mouse,
            windows, EWord — plus entry/ISR asm
tools/      mkiso.py (El Torito ISO), eos_cwa.py (the EOS assembler)
build/      generated: eos.iso, kernel.bin (git-ignored)
eos.iso     release boot CD (also used for VirtualBox)
```

## Version history

- **0.0.8** — renamed Diev→EOS; PS/2 mouse, windows, EWord dictionary.
- 0.0.7 — EOSCWA assembler + `exec` programs.
- 0.0.6 — El Torito ISO + more shell commands.
- 0.0.5 — div browser/editor + Turkish-Q layout.
- 0.0.4 — IDT/fault handler, folders, arrow keys.
- 0.0.3 — RMFS RAM filesystem.
- 0.0.2 — keyboard + shell.
- 0.0.1 — first boot: "hello world!".

## License

GPLv3 or later — see `LICENSE`. SPDX headers on sources.
