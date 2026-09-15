; EOS OS - Stage 1 Bootloader (512 bytes, BIOS)
; Copyright (C) 2026 EOS contributors
; SPDX-License-Identifier: GPL-3.0-or-later
; See LICENSE for details.
; Prints "EOS loading...", copies the kernel from the boot image to
; 0x1000, then enters 32-bit protected mode.
; El Torito no-emulation boot loads boot.bin + kernel.bin to 0x7C00,
; so no disk reads are needed at all: plain memcpy. DL (boot drive)
; is stashed at 0x0500 for the kernel's `iso` command.
; Assemble: nasm -f bin boot.asm -o boot.bin

[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl            ; BIOS boot drive number
    mov [0x0500], dl                ; stash for kernel (0x0500 is free RAM)

    mov ah, 0x00                    ; set video mode 80x25 text (clears screen)
    mov al, 0x03
    int 0x10

    mov si, msg_loading
    call print_string               ; "EOS loading..." at row 0

    ; --- Copy kernel 0x7E00 -> 0x1000 (71 sectors = 36352 bytes) ---
    ; Forward copy is safe: dest (0x1000) < src (0x7E00), same stride,
    ; so dest never overwrites unread source.
    ; IRQs stay OFF for the whole copy: SeaBIOS timer/disk handlers
    ; cannot run on memory we are rewriting underneath them.
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, 0x7E00
    mov di, 0x1000
    mov cx, 48 * 512 / 2            ; word count: 24 KB max!
    ; ^^^ HARD CEILING: dest [0x1000..0x7000) must stay clear of our
    ; stack [0x7B00..0x7C00) and code [0x7C00..). Grow past this and
    ; the copy eats its own code -> silent death. mkiso.py enforces it.
    rep movsw
.load_done:
    sti                             ; copy done, IRQs welcome again

    ; --- Enable A20 (fast gate, port 0x92) ---
    in al, 0x92
    or al, 2
    out 0x92, al

    ; --- Enter protected mode ---
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_start

; ---------------- 16-bit helpers ----------------
print_string:                       ; DS:SI = zero-terminated string
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    popa
    ret

; ---------------- Data ----------------
msg_loading db "EOS loading...", 13, 10, 0
boot_drive  db 0

align 4
gdt_start:
    dq 0x0000000000000000            ; null
    dq 0x00CF9A000000FFFF            ; code: base 0, limit 4GB, exec/read
    dq 0x00CF92000000FFFF            ; data: base 0, limit 4GB, read/write
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ---------------- 32-bit entry ----------------
[BITS 32]
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp 0x08:0x1000                 ; jump to kernel (linked at 0x1000)

times 510-($-$$) db 0
dw 0xAA55
