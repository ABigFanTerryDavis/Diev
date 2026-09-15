cmake_minimum_required(VERSION 3.15)
project(EOS C)

# EOS OS build (Windows/MinGW native, no cross-compiler needed):
# Copyright (C) 2026 EOS contributors
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
# Run:   qemu-system-x86_64 -cdrom build/eos.iso -boot order=d -m 128M
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
set(MOUSE_C   ${CMAKE_SOURCE_DIR}/kernel/mouse.c)
set(WIN_C     ${CMAKE_SOURCE_DIR}/kernel/win.c)
set(EWORD_C   ${CMAKE_SOURCE_DIR}/kernel/eword.c)
set(LINKER_PE ${CMAKE_SOURCE_DIR}/kernel/linker_pe.ld)
set(MKISO_PY  ${CMAKE_SOURCE_DIR}/tools/mkiso.py)
set(EOSCWA_PY   ${CMAKE_SOURCE_DIR}/tools/eos_cwa.py)
set(DEMO_ASM  ${CMAKE_SOURCE_DIR}/kernel/demo.asm)

set(BOOT_BIN   ${CMAKE_BINARY_DIR}/boot.bin)
set(ENTRY_O    ${CMAKE_BINARY_DIR}/kernel_entry.o)
set(KERNEL_O   ${CMAKE_BINARY_DIR}/kernel.o)
set(RMFS_O     ${CMAKE_BINARY_DIR}/rmfs.o)
set(IDT_O      ${CMAKE_BINARY_DIR}/idt.o)
set(DIV_O      ${CMAKE_BINARY_DIR}/div.o)
set(ISR_O      ${CMAKE_BINARY_DIR}/isr.o)
set(MOUSE_O    ${CMAKE_BINARY_DIR}/mouse.o)
set(WIN_O      ${CMAKE_BINARY_DIR}/win.o)
set(EWORD_O    ${CMAKE_BINARY_DIR}/eword.o)
set(KERNEL_PE  ${CMAKE_BINARY_DIR}/kernel.pe)
set(KERNEL_BIN ${CMAKE_BINARY_DIR}/kernel.bin)
set(EOS_ISO   ${CMAKE_BINARY_DIR}/eos.iso)
set(DEMO_BIN   ${CMAKE_BINARY_DIR}/demo.bin)
set(DEMO_H     ${CMAKE_BINARY_DIR}/demo_prog.h)
set(EOSCWA_STAMP ${CMAKE_BINARY_DIR}/EOSCWA.stamp)

# 1. Bootloader: 512-byte flat binary
add_custom_command(
    OUTPUT ${BOOT_BIN}
    COMMAND ${NASM} -f bin ${BOOT_ASM} -o ${BOOT_BIN}
    DEPENDS ${BOOT_ASM}
    COMMENT "Assembling EOS bootloader"
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
            -I${CMAKE_SOURCE_DIR}/kernel -I${CMAKE_BINARY_DIR}
            -Wall -Os -c ${KERNEL_C} -o ${KERNEL_O}
    DEPENDS ${KERNEL_C} ${DEMO_H} ${CMAKE_SOURCE_DIR}/kernel/kernel.h ${CMAKE_SOURCE_DIR}/kernel/idt.h ${CMAKE_SOURCE_DIR}/kernel/rmfs.h ${CMAKE_SOURCE_DIR}/kernel/div.h
    COMMENT "Compiling EOS kernel"
    VERBATIM
)

# 3b. RMFS (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${RMFS_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -Os -c ${RMFS_C} -o ${RMFS_O}
    DEPENDS ${RMFS_C} ${CMAKE_SOURCE_DIR}/kernel/rmfs.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h
    COMMENT "Compiling EOS RMFS"
    VERBATIM
)

# 3c. IDT (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${IDT_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -Os -c ${IDT_C} -o ${IDT_O}
    DEPENDS ${IDT_C} ${CMAKE_SOURCE_DIR}/kernel/idt.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h
    COMMENT "Compiling EOS IDT"
    VERBATIM
)

# 3d. div browser/editor (32-bit freestanding COFF object)
add_custom_command(
    OUTPUT ${DIV_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -Os -c ${DIV_C} -o ${DIV_O}
    DEPENDS ${DIV_C} ${CMAKE_SOURCE_DIR}/kernel/div.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h ${CMAKE_SOURCE_DIR}/kernel/rmfs.h
    COMMENT "Compiling EOS div"
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

# 3f. mouse, window manager, EWord (32-bit freestanding COFF objects)
add_custom_command(
    OUTPUT ${MOUSE_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -Os -c ${MOUSE_C} -o ${MOUSE_O}
    DEPENDS ${MOUSE_C} ${CMAKE_SOURCE_DIR}/kernel/mouse.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h
    COMMENT "Compiling EOS mouse"
    VERBATIM
)

add_custom_command(
    OUTPUT ${WIN_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -Os -c ${WIN_C} -o ${WIN_O}
    DEPENDS ${WIN_C} ${CMAKE_SOURCE_DIR}/kernel/win.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h ${CMAKE_SOURCE_DIR}/kernel/mouse.h
    COMMENT "Compiling EOS windows"
    VERBATIM
)

add_custom_command(
    OUTPUT ${EWORD_O}
    COMMAND gcc -m32 -ffreestanding -nostdlib -nostartfiles
            -fno-pie -fno-stack-protector -mno-red-zone -fno-zero-initialized-in-bss
            -march=i486 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
            -I${CMAKE_SOURCE_DIR}/kernel
            -Wall -Os -c ${EWORD_C} -o ${EWORD_O}
    DEPENDS ${EWORD_C} ${CMAKE_SOURCE_DIR}/kernel/eword.h ${CMAKE_SOURCE_DIR}/kernel/win.h ${CMAKE_SOURCE_DIR}/kernel/kernel.h ${CMAKE_SOURCE_DIR}/kernel/mouse.h
    COMMENT "Compiling EWord"
    VERBATIM
)

# 3g. demo program (assembled by EOSCWA itself: EOSCWA eats its own dogfood)
add_custom_command(
    OUTPUT ${DEMO_BIN}
    COMMAND ${PYTHON} ${EOSCWA_PY} -f bin ${DEMO_ASM} -o ${DEMO_BIN}
    DEPENDS ${DEMO_ASM} ${EOSCWA_PY}
    COMMENT "Assembling demo with EOSCWA"
    VERBATIM
)

# 3h. demo bytes as a C header (seeded into RMFS at boot)
add_custom_command(
    OUTPUT ${DEMO_H}
    COMMAND ${PYTHON} -c "d=open(r'${DEMO_BIN}','rb').read(); open(r'${DEMO_H}','w').write('/* generated from kernel/demo.asm - do not edit */\nstatic const unsigned char demo_prog[] = {%s};\nstatic const unsigned int demo_prog_len = %d;\n' % (','.join('0x%02x' % b for b in d), len(d)))"
    DEPENDS ${DEMO_BIN}
    COMMENT "Generating demo_prog.h"
    VERBATIM
)

# 3i. EOSCWA self-proof: NASM reference vs EOSCWA output must be identical
#     (COFF timestamps masked). Build fails if EOSCWA drifts.
add_custom_command(
    OUTPUT ${EOSCWA_STAMP}
    COMMAND ${NASM} -f bin ${BOOT_ASM} -o ${CMAKE_BINARY_DIR}/ref_boot.bin
    COMMAND ${PYTHON} ${EOSCWA_PY} -f bin ${BOOT_ASM} -o ${CMAKE_BINARY_DIR}/EOSCWA_boot.bin --expect ${CMAKE_BINARY_DIR}/ref_boot.bin
    COMMAND ${NASM} -f win32 ${ENTRY_ASM} -o ${CMAKE_BINARY_DIR}/ref_entry.o
    COMMAND ${PYTHON} ${EOSCWA_PY} -f win32 ${ENTRY_ASM} -o ${CMAKE_BINARY_DIR}/EOSCWA_entry.o --expect ${CMAKE_BINARY_DIR}/ref_entry.o
    COMMAND ${NASM} -f win32 ${ISR_ASM} -o ${CMAKE_BINARY_DIR}/ref_isr.o
    COMMAND ${PYTHON} ${EOSCWA_PY} -f win32 ${ISR_ASM} -o ${CMAKE_BINARY_DIR}/EOSCWA_isr.o --expect ${CMAKE_BINARY_DIR}/ref_isr.o
    COMMAND ${CMAKE_COMMAND} -E touch ${EOSCWA_STAMP}
    DEPENDS ${BOOT_ASM} ${ENTRY_ASM} ${ISR_ASM} ${EOSCWA_PY}
    COMMENT "Proving EOSCWA == NASM"
    VERBATIM
)

add_custom_target(eoscwa_check ALL DEPENDS ${EOSCWA_STAMP} ${DEMO_H})

# 4. Link kernel (PE, single merged section, image-base 0x1000 = load address)
add_custom_command(
    OUTPUT ${KERNEL_PE}
    COMMAND ${LD} -m i386pe --image-base 0x1000 -T ${LINKER_PE}
            -o ${KERNEL_PE} ${ENTRY_O} ${KERNEL_O} ${RMFS_O} ${IDT_O} ${DIV_O} ${MOUSE_O} ${WIN_O} ${EWORD_O} ${ISR_O}
    DEPENDS ${ENTRY_O} ${KERNEL_O} ${RMFS_O} ${IDT_O} ${DIV_O} ${MOUSE_O} ${WIN_O} ${EWORD_O} ${ISR_O} ${LINKER_PE}
    COMMENT "Linking EOS kernel"
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
    OUTPUT ${EOS_ISO}
    COMMAND ${PYTHON} ${MKISO_PY} ${BOOT_BIN} ${KERNEL_BIN}
            ${CMAKE_SOURCE_DIR}/LICENSE ${EOS_ISO}
    DEPENDS ${BOOT_BIN} ${KERNEL_BIN} ${MKISO_PY} ${CMAKE_SOURCE_DIR}/LICENSE
    COMMENT "Creating eos.iso (El Torito)"
    VERBATIM
)

add_custom_target(diev ALL DEPENDS ${EOS_ISO})
