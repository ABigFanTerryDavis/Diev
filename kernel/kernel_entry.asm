; Diev OS - Kernel entry (32-bit)
; Copyright (C) 2026 Diev contributors
; SPDX-License-Identifier: GPL-3.0-or-later
; See LICENSE for details.
; Sets up stack and calls kernel_main.
; Assemble (Windows/MinGW): nasm -f win32 kernel_entry.asm -o kernel_entry.o
; Assemble (Linux/ELF):     nasm -f elf32 kernel_entry.asm -o kernel_entry.o
; NOTE: win32 COFF prefixes C symbols with '_', so we alias both names.

[BITS 32]
global _start

%ifidn __OUTPUT_FORMAT__, win32
  extern _kernel_main
  %define KERNEL_MAIN _kernel_main
%else
  extern kernel_main
  %define KERNEL_MAIN kernel_main
%endif
section .text
_start:
    mov esp, 0x90000
    cld                         ; string ops expect direction flag clear
    ; NOTE: no .bss clearing here on purpose. The MinGW PE linker does not
    ; give reliable absolute addresses for script-defined bss symbols, so
    ; .bss users initialize themselves instead (see rmfs_init()).
    ; Compile with -fno-zero-initialized-in-bss so "= 0" globals land in
    ; .data (which the image carries) instead of .bss.
    call KERNEL_MAIN
.hang:
    cli
    hlt
    jmp .hang
