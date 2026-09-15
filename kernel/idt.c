/* EOS OS - IDT setup + fault handler (prints EOS ERR E4, halts).
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#include "idt.h"
#include "kernel.h"
#include "mouse.h"

typedef struct {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[48];

static void (* const isr_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static void (* const irq_stubs[16])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15,
};

static void idt_set_gate(int n, void (*handler)(void)) {
    uint32_t base = (uint32_t)handler;
    idt[n].base_lo = (uint16_t)(base & 0xFFFF);
    idt[n].sel     = 0x08; /* bootloader code segment */
    idt[n].zero    = 0;
    idt[n].flags   = 0x8E; /* present, ring 0, 32-bit interrupt gate */
    idt[n].base_hi = (uint16_t)((base >> 16) & 0xFFFF);
}

void idt_install(void) {
    int i;
    for (i = 0; i < 32; i++)
        idt_set_gate(i, isr_stubs[i]);
    for (i = 0; i < 16; i++)
        idt_set_gate(0x20 + i, irq_stubs[i]);
    idt_ptr_t ptr;
    ptr.limit = (uint16_t)(sizeof(idt) - 1);
    ptr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" :: "m"(ptr));
}

void irq_handler(fault_regs_t *r) {
    if (r->int_no == 0x2C)
        mouse_irq();
    /* all other IRQs are masked; ignore (EOI already sent by stub) */
}

static const char *fault_name(uint32_t n) {
    static const char *names[32] = {
        "Divide by zero",            /* 0 */
        "Debug",                     /* 1 */
        "Non-maskable interrupt",    /* 2 */
        "Breakpoint",                /* 3 */
        "Overflow",                  /* 4 */
        "Bound range exceeded",      /* 5 */
        "Invalid opcode",            /* 6 */
        "Device not available",      /* 7 */
        "Double fault",              /* 8 */
        "Coprocessor segment overrun", /* 9 */
        "Invalid TSS",               /* 10 */
        "Segment not present",       /* 11 */
        "Stack segment fault",       /* 12 */
        "General protection fault",  /* 13 */
        "Page fault",                /* 14 */
        "Reserved",                  /* 15 */
        "x87 floating point",        /* 16 */
        "Alignment check",           /* 17 */
        "Machine check",             /* 18 */
        "SIMD floating point",       /* 19 */
        "Virtualization",            /* 20 */
        "Reserved",                  /* 21 */
        "Reserved",                  /* 22 */
        "Reserved",                  /* 23 */
        "Reserved",                  /* 24 */
        "Reserved",                  /* 25 */
        "Reserved",                  /* 26 */
        "Reserved",                  /* 27 */
        "Reserved",                  /* 28 */
        "Reserved",                  /* 29 */
        "Reserved",                  /* 30 */
        "Reserved",                  /* 31 */
    };
    return n < 32 ? names[n] : "Unknown";
}

void fault_handler(fault_regs_t *r) {
    vga_putchar('\n', C_RED_ON_BLACK);
    vga_print("EOS ERR E4: ", C_RED_ON_BLACK);
    vga_print(fault_name(r->int_no), C_RED_ON_BLACK);
    vga_print(" err=", C_RED_ON_BLACK);
    vga_print_hex(r->err_code, C_RED_ON_BLACK);
    vga_print(" eip=", C_RED_ON_BLACK);
    vga_print_hex(r->eip, C_RED_ON_BLACK);
    vga_putchar('\n', C_RED_ON_BLACK);
    vga_print("EOS halted.", C_RED_ON_BLACK);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
