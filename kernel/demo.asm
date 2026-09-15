; EOS OS - demo user program (flat binary for `exec`).
; Copyright (C) 2026 EOS contributors
; SPDX-License-Identifier: GPL-3.0-or-later
; See LICENSE for details.
; ABI: loaded at 0x20000, called at base+4 (after magic) in 32-bit
; flat mode (DS/ES base 0). Preserve EBX ESI EDI EBP. Print on rows
; 10+ only (shell owns rows above). Return with ret.
; Assemble: dasm.py -f bin demo.asm -o demo.bin  (or nasm -f bin)

[BITS 32]
[ORG 0x20000]
db 'E', 'O', 'S', '!'
_demo:
pushad
mov esi, msg
mov edi, 0xB8000 + 10 * 160
.next:
mov al, [esi]
test al, al
jz .done
mov ah, 0x0A
mov [edi], ax
add edi, 2
inc esi
jmp .next
.done:
popad
ret
msg db "hi from exec!", 0
