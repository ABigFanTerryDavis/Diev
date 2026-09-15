/* EOS OS PE linker script (Windows/MinGW fallback).
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Merges everything into one contiguous section at 0x1000 so that
 * objcopy -O binary yields a flat kernel bootable at 0x1000.
 * (MinGW ld only supports PE emulations, not elf_i386.)
 */
OUTPUT_FORMAT("pei-i386");
ENTRY(_start);

SECTIONS
{
    . = 0x1000;

    .text : {
        *(.text*)
        *(.rdata*)
        *(.rodata*)
        *(.data*)
        *(.eh_frame*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.idata*)
        *(.reloc*)
        *(.comment*)
        *(.note*)
        *(.pdata*)
        *(.xdata*)
    }
}
