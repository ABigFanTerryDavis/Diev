/* EOS OS - IDT + CPU exception handlers (32-bit protected mode).
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 *
 * Routes CPU faults (vectors 0-31) to EOS ERR E4 instead of triple-faulting.
 * IRQs stay off (IF is clear); only exceptions are handled here.
 */
#ifndef EOS_IDT_H
#define EOS_IDT_H

#include <stdint.h>

/* Pushed by isr.asm: segment regs, pusha, int number, error code, then CPU frame. */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} fault_regs_t;

void idt_install(void);
void fault_handler(fault_regs_t *r); /* called from isr.asm, never returns */
void irq_handler(fault_regs_t *r);   /* hardware IRQs 0x20-0x2F, returns */

/* ISR entry points (defined in isr.asm). */
extern void isr0(void);  extern void isr1(void);
extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);
extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void);
extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void);
extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void);
extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* IRQ stubs (defined in isr.asm, vectors 0x20-0x2F after PIC remap). */
extern void irq0(void);  extern void irq1(void);
extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);
extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void);
extern void irq14(void); extern void irq15(void);

#endif
