; EOS OS - ISR stubs for CPU exceptions 0-31 (32-bit).
; Copyright (C) 2026 EOS contributors
; SPDX-License-Identifier: GPL-3.0-or-later
; See LICENSE for details.
; Assemble (Windows/MinGW): nasm -f win32 isr.asm -o isr.o
; Assemble (Linux/ELF):     nasm -f elf32 isr.asm -o isr.o

[BITS 32]
section .text

%ifidn __OUTPUT_FORMAT__, win32
  extern _fault_handler
  %define FAULT_HANDLER _fault_handler
  extern _irq_handler
  %define IRQ_HANDLER _irq_handler
%else
  extern fault_handler
  %define FAULT_HANDLER fault_handler
  extern irq_handler
  %define IRQ_HANDLER irq_handler
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

; Hardware IRQs 0-15 -> vectors 0x20-0x2F (PIC remapped by kernel).
; No CPU error code: push dummy 0. Master-only and slave variants so
; EOI stays correct without reading the stack.
%macro IRQ_M 1
  global _irq%1
  _irq%1:
    cli
    push dword 0
    push dword 0x20 + %1
    jmp _irq_common_m
%endmacro

%macro IRQ_S 1
  global _irq%1
  _irq%1:
    cli
    push dword 0
    push dword 0x20 + %1
    jmp _irq_common_s
%endmacro

IRQ_M 0
IRQ_M 1
IRQ_M 2
IRQ_M 3
IRQ_M 4
IRQ_M 5
IRQ_M 6
IRQ_M 7
IRQ_S 8
IRQ_S 9
IRQ_S 10
IRQ_S 11
IRQ_S 12
IRQ_S 13
IRQ_S 14
IRQ_S 15

_irq_common_m:
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
    push esp                ; fault_regs_t *r (int_no readable)
    call IRQ_HANDLER
    add esp, 4
    mov al, 0x20
    out 0x20, al            ; EOI master
    popa
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iret

_irq_common_s:
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
    push esp
    call IRQ_HANDLER
    add esp, 4
    mov al, 0x20
    out 0xA0, al            ; EOI slave first
    out 0x20, al            ; then master
    popa
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8
    iret
