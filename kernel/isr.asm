; Diev OS - ISR stubs for CPU exceptions 0-31 (32-bit).
; Copyright (C) 2026 Diev contributors
; SPDX-License-Identifier: GPL-3.0-or-later
; See LICENSE for details.
; Assemble (Windows/MinGW): nasm -f win32 isr.asm -o isr.o
; Assemble (Linux/ELF):     nasm -f elf32 isr.asm -o isr.o

[BITS 32]
section .text

%ifidn __OUTPUT_FORMAT__, win32
  extern _fault_handler
  %define FAULT_HANDLER _fault_handler
%else
  extern fault_handler
  %define FAULT_HANDLER fault_handler
%endif

; Exceptions WITHOUT CPU-pushed error code: push dummy 0 first.
%macro ISR_NOERR 1
  global _isr%1
  _isr%1:
    cli
    push dword 0
    push dword %1
    jmp _isr_common_stub
%endmacro

; Exceptions WITH CPU-pushed error code: only push the vector number.
%macro ISR_ERR 1
  global _isr%1
  _isr%1:
    cli
    push dword %1
    jmp _isr_common_stub
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; Common entry: build fault_regs_t on the stack, call C handler.
_isr_common_stub:
    push ds
    push es
    push fs
    push gs
    pusha
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                ; fault_regs_t *r
    call FAULT_HANDLER
    add esp, 4
    popa
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8              ; int_no + err_code
    iret                    ; handler halts, but stay correct
