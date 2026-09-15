/* Diev OS linker script - kernel linked at 0x1000 (where bootloader loads it)
 * Copyright (C) 2026 Diev contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
OUTPUT_FORMAT("elf32-i386");
ENTRY(_start);

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }
}
