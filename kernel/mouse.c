/* EOS - PS/2 mouse implementation.
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#include "mouse.h"
#include "kernel.h"

static unsigned char inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}

static int mx = 40, my = 12;    /* cursor cell */
static int mleft = 0;
static int have_mouse = 0;
static int shown = 0;
static unsigned short under = 0; /* cell saved under cursor */

/* wait for controller: out=1 ready to accept command, out=0 data ready */
static int mwait(int out) {
    for (int i = 0; i < 100000; i++) {
        unsigned char s = inb(0x64);
        if (out) {
            if (!(s & 2)) return 1;
        } else {
            if (s & 1) return 1;
        }
    }
    return 0;
}

static void aux_write(unsigned char b) {
    mwait(1);
    outb(0x64, 0xD4);
    mwait(1);
    outb(0x60, b);
}

static int aux_read(void) {
    if (!mwait(0)) return -1;
    return inb(0x60);
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);   /* master vectors 0x20-0x27 */
    outb(0xA1, 0x28);   /* slave vectors 0x28-0x2F */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFB);   /* mask all but cascade */
    outb(0xA1, 0xEF);   /* mask all but mouse (IRQ12) */
}

int mouse_init(void) {
    int a1, a2, cmd;
    pic_remap();
    /* enable IRQ12 (+IRQ1) in the controller command byte */
    if (!mwait(1)) return 0;
    outb(0x64, 0x20);
    if (!mwait(0)) return 0;
    cmd = inb(0x60) | 3;
    if (!mwait(1)) return 0;
    outb(0x64, 0x60);
    if (!mwait(1)) return 0;
    outb(0x60, (unsigned char)cmd);
    outb(0x64, 0xA8);               /* enable aux port */
    aux_write(0xF6);                /* defaults */
    a1 = aux_read();
    if (a1 != 0xFA) return 0;
    aux_write(0xF4);                /* enable packets */
    a2 = aux_read();
    if (a2 != 0xFA) return 0;
    (void)a1;
    (void)a2;
    have_mouse = 1;
    __asm__ volatile ("sti");
    return 1;
}

static unsigned char pkt[3];
static int phase = 0;

void mouse_feed(unsigned char b) {
    if (phase == 0 && !(b & 8)) return; /* resync: bit 3 always set */
    pkt[phase++] = b;
    if (phase < 3) return;
    phase = 0;
    {
        /* pkt bytes are 9-bit two's complement; (signed char) already
           carries the sign, the sign bits in byte 0 just confirm it. */
        int dx = (int)(signed char)pkt[1];
        int dy = (int)(signed char)pkt[2];
        mouse_hide();
        mleft = (pkt[0] & 1) ? 1 : 0;
        mx += dx / 2;
        my -= dy / 2;
        if (mx < 0) mx = 0;
        if (mx > 79) mx = 79;
        if (my < 0) my = 0;
        if (my > 24) my = 24;
        mouse_draw();
    }
}

void mouse_irq(void) {
    if (!(inb(0x64) & 0x20)) return; /* not aux data; leave for keyboard */
    mouse_feed(inb(0x60));
}

int mouse_present(void) { return have_mouse; }
int mouse_x(void) { return mx; }
int mouse_y(void) { return my; }
int mouse_left(void) { return mleft; }

void mouse_draw(void) {
    volatile unsigned short *vga = (volatile unsigned short *)0xB8000;
    int pos = my * 80 + mx;
    if (!shown) under = vga[pos];
    vga[pos] = (unsigned short)' ' | ((unsigned short)0xF0 << 8);
    shown = 1;
}

void mouse_hide(void) {
    volatile unsigned short *vga = (volatile unsigned short *)0xB8000;
    if (shown) {
        vga[my * 80 + mx] = under;
        shown = 0;
    }
}
