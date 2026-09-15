cmake_minimum_required(VERSION 3.15)
project(Diev C)

# Diev OS build (Windows/MinGW native, no cross-compiler needed):
# Copyright (C) 2026 Diev contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#   bootloader : NASM  -> 512-byte flat boot.bin
#   kernel     : gcc -m32 (COFF) + nasm -f win32 (COFF)
#                -> ld -m i386pe (merged sections at image-base 0x1000)
#                -> objcopy -O binary (flat kernel.bin, entry at offset 0)
#   image      : El Torito ISO via tools/mkiso.py (pure Python, no xorriso)
#                boot.bin + kernel travel AS the no-emulation boot image,
#                so booting needs zero disk reads.
#
# Build: cmake -B build -G Ninja && cmake --build build
# Run:   qemu-system-x86_64 -cdrom build/diev.iso -boot order=d -m 128M
#
# (On Linux with an ELF toolchain, swap win32->elf32, i386pe->elf_i386,
#  --image-base->-Ttext and use kernel/linker.ld instead of linker_pe.ld.)

find_program(NASM nasm REQUIRED)
find_program(LD ld REQUIRED)
find_program(OBJCOPY objcopy REQUIRED)
find_program(PYTHON python REQUIRED)

set(BOOT_ASM  ${CMAKE_SOURCE_DIR}/boot/boot.asm)
set(ENTRY_ASM ${CMAKE_SOURCE_DIR}/kernel/kernel_entry.asm)
set(KERNEL_C  ${CMAKE_SOURCE_DIR}/kernel/kernel.c)
set(RMFS_C    ${CMAKE_SOURCE_DIR}/kernel/rmfs.c)
set(IDT_C     ${CMAKE_SOURCE_DIR}/kernel/idt.c)
set(DIV_C     ${CMAKE_SOURCE_DIR}/kernel/div.c)
set(ISR_ASM   ${CMAKE_SOURCE_DIR}/kernel/isr.asm)
set(LINKER_PE ${CMAKE_SOURCE_DIR}/kernel/linker_pe.ld)
set(MKISO_PY  ${CMAKE_SOURCE_DIR}/tools/mkiso.py)

set(BOOT_BIN   ${CMAKE_BINARY_DIR}/boot.bin)
set(ENTRY_O    ${CMAKE_BINARY_DIR}/kernel_entry.o)
set(KERNEL_O   ${CMAKE_BINARY_DIR}/kernel.o)
set(RMFS_O     ${CMAKE_BINARY_DIR}/rmfs.o)
set(IDT_O      ${CMAKE_BINARY_DIR}/idt.o)
set(DIV_O      ${CMAKE_BINARY_DIR}/div.o)
set(ISR_O      ${CMAKE_BINARY_DIR}/isr.o)
set(KERNEL_PE  ${CMAKE_BINARY_DIR}/kernel.pe)
set(KERNEL_BIN ${CMAKE_BINARY_DIR}/kernel.bin)
set(DIEV_ISO   ${CMAKE_BINARY_DIR}/diev.iso)

# 1. Bootloader: 512-byte flat binary
add_custom_command(
    OUTPUT ${BOOT_BIN}
    COMMAND ${NASM} -f bin ${BOOT_ASM} -o ${BOOT_BIN}
    DEPENDS ${BOOT_ASM}
    COMMENT "Assembling Diev bootloader"
    VERBATIM
)

# 2. Kernel entry (32-bit COFF object, matches MinGW gcc output)
add_custom_command(
    OUTPUT ${ENTRY_O}
    COMMAND ${NASM} -f win32 ${ENTRY_ASM} -o ${ENTRY_O}
    DEPENDS ${ENTRY_ASM}
    COMMENT "Assembling kernel entry"
    VERBATIM
)

# 3. Kernel C (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${KERNEL_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -O2 -c ${KERNEL_C} -o ${KERNEL_O}
    DEPENDS ${KERNEL_C} ${CMAKE_SOURCE_DIR}/kernel/kernel.h ${CMAKE_SOURCE_DIR}/kernel/idt.h ${CMAKE_SOURCE_DIR}/kernel/rmfs.h ${CMAKE_SOURCE_DIR}/kernel/div.h
    COMMENT "Compiling Diev kernel"
    VERBATIM
)

# 3b. RMFS (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${RMFS_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -O2 -c ${RMFS_C} -o ${RMFS_O}
    DEPENDS ${RMFS_C} ${CMAKE_SOURCE_DIR}/kernel/rmfs.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h
    COMMENT "Compiling Diev RMFS"
    VERBATIM
)

# 3c. IDT (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${IDT_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -O2 -c ${IDT_C} -o ${IDT_O}
    DEPENDS ${IDT_C} ${CMAKE_SOURCE_DIR}/kernel/idt.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h
    COMMENT "Compiling Diev IDT"
    VERBATIM
)

# 3d. div browser/editor (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${DIV_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -O2 -c ${DIV_C} -o ${DIV_O}
    DEPENDS ${DIV_C} ${CMAKE_SOURCE_DIR}/kernel/div.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h ${CMAKE_SOURCE_DIR}/kernel/rmfs.h
    COMMENT "Compiling Diev div"
    VERBATIM
)

# 3e. ISR stubs (32-bit COFF object, matches MinGW gcc output)
add_custom_command(
    OUTPUT ${ISR_O}
    COMMAND ${NASM} -f win32 ${ISR_ASM} -o ${ISR_O}
    DEPENDS ${ISR_ASM}
    COMMENT "Assembling ISR stubs"
    VERBATIM
)

# 4. Link kernel (PE, single merged section, image-base 0x1000 = load address)
add_custom_command(
    OUTPUT ${KERNEL_PE}
    COMMAND ${LD} -m i386pe --image-base 0x1000 -T ${LINKER_PE}
            -o ${KERNEL_PE} ${ENTRY_O} ${KERNEL_O} ${RMFS_O} ${IDT_O} ${DIV_O} ${ISR_O}
    DEPENDS ${ENTRY_O} ${KERNEL_O} ${RMFS_O} ${IDT_O} ${DIV_O} ${ISR_O} ${LINKER_PE}
    COMMENT "Linking Diev kernel"
    VERBATIM
)

# 5. Kernel flat binary (headers stripped, entry _start at offset 0)
add_custom_command(
    OUTPUT ${KERNEL_BIN}
    COMMAND ${OBJCOPY} -O binary ${KERNEL_PE} ${KERNEL_BIN}
    DEPENDS ${KERNEL_PE}
    COMMENT "Objcopy kernel to flat binary"
    VERBATIM
)

# 6. El Torito ISO (pure Python, no xorriso needed).
#    The bootloader + kernel travel AS the no-emulation boot image,
#    so booting needs zero disk reads: BIOS loads it all to 0x7C00.
add_custom_command(
    OUTPUT ${DIEV_ISO}
    COMMAND ${PYTHON} ${MKISO_PY} ${BOOT_BIN} ${KERNEL_BIN}
            ${CMAKE_SOURCE_DIR}/LICENSE ${DIEV_ISO}
    DEPENDS ${BOOT_BIN} ${KERNEL_BIN} ${MKISO_PY} ${CMAKE_SOURCE_DIR}/LICENSE
    COMMENT "Creating diev.iso (El Torito)"
    VERBATIM
)

add_custom_target(diev ALL DEPENDS ${DIEV_ISO})
