/* EOS - Kernel (32-bit, freestanding)
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 * Boot flow: bootloader prints "EOS loading..." via BIOS,
 * then this kernel prints "hello world!" via VGA and runs a tiny shell.
 * 5 defined errors, shown as EOS ERR E1..E5, then halt.
 */

#include "kernel.h"
#include "idt.h"
#include "rmfs.h"
#include "div.h"
#include "eword.h"
#include "mouse.h"
#include "demo_prog.h"

#define VGA_ADDR 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define EOS_VER "EOS v0.0.8"

static volatile unsigned short *vga = (volatile unsigned short *)VGA_ADDR;
static unsigned char cursor_row = 1; /* row 0 holds "EOS loading..." from bootloader */
static unsigned char cursor_col = 0;
static unsigned char boot_drive = 0xFF; /* stashed by bootloader at 0x0500 */

/* --- Port I/O --- */
static unsigned char inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outb(unsigned short port, unsigned char v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}

/* --- VGA driver --- */
void vga_update_cursor(void) {
    unsigned short pos = (unsigned short)(cursor_row * VGA_COLS + cursor_col);
    outb(0x3D4, 14);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
    outb(0x3D4, 15);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
}

void vga_set_cursor(unsigned char row, unsigned char col) {
    if (row < VGA_ROWS) cursor_row = row;
    if (col < VGA_COLS) cursor_col = col;
    vga_update_cursor();
}

static void vga_scroll(void) {
    for (int r = 0; r < VGA_ROWS - 1; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga[r * VGA_COLS + c] = vga[(r + 1) * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        vga[(VGA_ROWS - 1) * VGA_COLS + c] = (unsigned short)' ' | ((unsigned short)C_GREY_ON_BLACK << 8);
}

static void vga_newline(void) {
    cursor_col = 0;
    if (cursor_row + 1 >= VGA_ROWS)
        vga_scroll();
    else
        cursor_row++;
    vga_update_cursor();
}

static void vga_put_at(char c, unsigned char color, unsigned char row, unsigned char col) {
    if (row >= VGA_ROWS || col >= VGA_COLS) return;
    vga[row * VGA_COLS + col] = (unsigned short)c | ((unsigned short)color << 8);
}

void vga_putchar(char c, unsigned char color) {
    if (c == '\n') {
        vga_newline();
        return;
    }
    if (c == '\r') {
        cursor_col = 0;
        vga_update_cursor();
        return;
    }
    vga_put_at(c, color, cursor_row, cursor_col);
    cursor_col++;
    if (cursor_col >= VGA_COLS)
        vga_newline();
    else
        vga_update_cursor();
}

void vga_print(const char *s, unsigned char color) {
    for (unsigned int i = 0; s[i]; i++)
        vga_putchar(s[i], color);
}

void vga_print_hex(unsigned int v, unsigned char color) {
    vga_print("0x", color);
    for (int i = 7; i >= 0; i--) {
        unsigned int d = (v >> (i * 4)) & 0xF;
        vga_putchar((char)(d < 10 ? '0' + d : 'A' + d - 10), color);
    }
}

void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = (unsigned short)' ' | ((unsigned short)C_GREY_ON_BLACK << 8);
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

static void vga_backspace(void) {
    if (cursor_col > 0) {
        cursor_col--;
    } else if (cursor_row > 0) {
        cursor_row--;
        cursor_col = VGA_COLS - 1;
    } else {
        return;
    }
    vga_put_at(' ', C_GREY_ON_BLACK, cursor_row, cursor_col);
    vga_update_cursor();
}

/* eos_error_t lives in kernel.h; messages stay local. */
static const char *eos_error_msg(eos_error_t code) {
    switch (code) {
        case EOS_E1_DISK:      return "EOS ERR E1: BOOT DISK READ FAIL";
        case EOS_E2_KERNEL:    return "EOS ERR E2: KERNEL LOAD FAIL";
        case EOS_E3_VGA:       return "EOS ERR E3: VGA FAULT";
        case EOS_E4_EXCEPTION: return "EOS ERR E4: UNKNOWN EXCEPTION";
        case EOS_E5_HALT:      return "EOS ERR E5: SYSTEM HALT FAULT";
        default:                return "EOS ERR E?: INVALID ERROR CODE";
    }
}

/* Panic: red error on VGA, then halt forever. No keyboard escape. */
void eos_panic(eos_error_t code) {
    cursor_row = VGA_ROWS - 2;
    cursor_col = 0;
    vga_print(eos_error_msg(code), C_RED_ON_BLACK);
    vga_putchar('\n', C_RED_ON_BLACK);
    vga_print("EOS halted.", C_RED_ON_BLACK);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* --- Small string helpers (no libc) --- */
static unsigned int kstrlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int kstrncmp(const char *a, const char *b, unsigned int n) {
    while (n-- > 0) {
        if (*a != *b) return (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}

/* --- Keyboard: PS/2 scancode set 1, polling, US + Turkish-Q layouts --- */
/* Arrow/special codes live in kernel.h (shared with div.c). */
static int kbd_is_tr = 0; /* 0 = US, 1 = Turkish-Q (`kb tr`) */
static const char kbd_normal[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9', [0x0B]='0',
    [0x0C]='-', [0x0D]='=',
    [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
    [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
    [0x1A]='[', [0x1B]=']',
    [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g',
    [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l', [0x27]=';',
    [0x28]='\'', [0x29]='`', [0x2B]='\\',
    [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b',
    [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.', [0x35]='/',
    [0x0E]='\b', [0x0F]='\t', [0x39]=' ',
};

static const char kbd_shifted[128] = {
    [0x02]='!', [0x03]='@', [0x04]='#', [0x05]='$', [0x06]='%',
    [0x07]='^', [0x08]='&', [0x09]='*', [0x0A]='(', [0x0B]=')',
    [0x0C]='_', [0x0D]='+',
    [0x10]='Q', [0x11]='W', [0x12]='E', [0x13]='R', [0x14]='T',
    [0x15]='Y', [0x16]='U', [0x17]='I', [0x18]='O', [0x19]='P',
    [0x1A]='{', [0x1B]='}',
    [0x1E]='A', [0x1F]='S', [0x20]='D', [0x21]='F', [0x22]='G',
    [0x23]='H', [0x24]='J', [0x25]='K', [0x26]='L', [0x27]=':',
    [0x28]='"', [0x29]='~', [0x2B]='|',
    [0x2C]='Z', [0x2D]='X', [0x2E]='C', [0x2F]='V', [0x30]='B',
    [0x31]='N', [0x32]='M', [0x33]='<', [0x34]='>', [0x35]='?',
    [0x0E]='\b', [0x0F]='\t', [0x39]=' ',
};

/* Turkish-Q (KBDTUQ): the key left of Right Shift is '.' (shift ':'),
 * '/' lives on Shift+7, ',' on the US-backslash key. CP437 has no
 * glyphs for g-g-s-S-i-I, so those fold to plain ASCII; c-o-u-C-O-U
 * and e-acute use real CP437 bytes. (A custom VGA font would fix the
 * folding; see vga font TODO.) */
static const char kbd_tr_normal[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9', [0x0B]='0',
    [0x0C]='*', [0x0D]='-',
    [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
    [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
    [0x1A]='g', [0x1B]='\x81',
    [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g',
    [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l', [0x27]='s',
    [0x28]='i', [0x29]='"', [0x2B]=',',
    [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b',
    [0x31]='n', [0x32]='m', [0x33]='\x94', [0x34]='\x87', [0x35]='.',
    [0x0E]='\b', [0x0F]='\t', [0x39]=' ',
};

static const char kbd_tr_shifted[128] = {
    [0x02]='!', [0x03]='\'', [0x04]='^', [0x05]='+', [0x06]='%',
    [0x07]='&', [0x08]='/', [0x09]='(', [0x0A]=')', [0x0B]='=',
    [0x0C]='?', [0x0D]='_',
    [0x10]='Q', [0x11]='W', [0x12]='E', [0x13]='R', [0x14]='T',
    [0x15]='Y', [0x16]='U', [0x17]='I', [0x18]='O', [0x19]='P',
    [0x1A]='G', [0x1B]='\x9A',
    [0x1E]='A', [0x1F]='S', [0x20]='D', [0x21]='F', [0x22]='G',
    [0x23]='H', [0x24]='J', [0x25]='K', [0x26]='L', [0x27]='S',
    [0x28]='I', [0x29]='\x82', [0x2B]=';',
    [0x2C]='Z', [0x2D]='X', [0x2E]='C', [0x2F]='V', [0x30]='B',
    [0x31]='N', [0x32]='M', [0x33]='\x99', [0x34]='\x80', [0x35]=':',
    [0x0E]='\b', [0x0F]='\t', [0x39]=' ',
};

void kbd_set_layout_tr(int tr) { kbd_is_tr = tr ? 1 : 0; }
int kbd_layout_is_tr(void) { return kbd_is_tr; }

static char kbd_map(unsigned char sc, int shifted) {
    if (kbd_is_tr) return shifted ? kbd_tr_shifted[sc] : kbd_tr_normal[sc];
    return shifted ? kbd_shifted[sc] : kbd_normal[sc];
}

/* Blocking read of one char. '\n' = Enter, '\b' = Backspace.
 * Skips PS/2 aux (mouse) bytes, feeding them to the mouse machine so
 * none are lost. Status+data read under cli: an IRQ between the two
 * would steal/mix the byte (that was the phantom-typing bug). */
static char kbd_getchar(void) {
    static int shift = 0;
    static int ext = 0;
    for (;;) {
        unsigned char st, sc;
        while (!(inb(0x64) & 0x01)) {
            __asm__ volatile ("pause");
        }
        __asm__ volatile ("cli");
        st = inb(0x64);
        if (!(st & 1)) {
            __asm__ volatile ("sti"); /* IRQ stole it; re-wait */
            continue;
        }
        sc = inb(0x60);
        __asm__ volatile ("sti");
        if (st & 0x20) {
            mouse_feed(sc);
            continue;
        }
        if (sc == 0xE0) { ext = 1; continue; }
        if (ext) {
            ext = 0;
            if (!(sc & 0x80)) {
                if (sc == 0x48) return KEY_UP;
                if (sc == 0x50) return KEY_DOWN;
                if (sc == 0x4B) return KEY_LEFT;
                if (sc == 0x4D) return KEY_RIGHT;
            }
            continue;
        }
        if (sc & 0x80) {                /* key release */
            unsigned char mk = (unsigned char)(sc & 0x7F);
            if (mk == 0x2A || mk == 0x36) shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (sc == 0x1C) return '\n';
        if (sc == 0x0E) return '\b';
        if (sc >= 128) continue;
        char c = kbd_map(sc, shift);
        if (c) return c;
    }
}

/* Raw key reader for div/editor: arrows, F2, Esc plus chars through the
 * active layout. Returns a char, KEY_*, K_F2 or K_ESC; sets *shift_state
 * (1 while left/right Shift is held). Extended-key releases ignored.
 * Returns 0 after ~25ms with no key (lets UI loops redraw for mouse).
 * Skips PS/2 aux (mouse) bytes. */
char div_getkey(int *shift_state) {
    static int shift = 0;
    static int ext = 0;
    unsigned int spin = 0;
    for (;;) {
        unsigned char st, sc;
        while (!(inb(0x64) & 0x01)) {
            __asm__ volatile ("pause");
            if (++spin > 500000) return 0;
        }
        __asm__ volatile ("cli");
        st = inb(0x64);
        if (!(st & 1)) {
            __asm__ volatile ("sti"); /* IRQ stole it; re-wait */
            continue;
        }
        sc = inb(0x60);
        __asm__ volatile ("sti");
        if (st & 0x20) {
            mouse_feed(sc);
            continue;
        }
        if (sc == 0xE0) { ext = 1; continue; }
        if (ext) {
            ext = 0;
            if (shift_state) *shift_state = shift;
            if (!(sc & 0x80)) {
                if (sc == 0x48) return KEY_UP;
                if (sc == 0x50) return KEY_DOWN;
                if (sc == 0x4B) return KEY_LEFT;
                if (sc == 0x4D) return KEY_RIGHT;
            }
            continue;
        }
        if (sc & 0x80) {
            unsigned char mk = (unsigned char)(sc & 0x7F);
            if (mk == 0x2A || mk == 0x36) shift = 0;
            continue;
        }
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (shift_state) *shift_state = shift;
        if (sc == 0x1C) return '\n';
        if (sc == 0x01) return K_ESC;
        if (sc == 0x3C) return K_F2;
        if (sc == 0x0E) return '\b';
        if (sc >= 128) continue;
        char c = kbd_map(sc, shift);
        if (c) return c;
    }
}

/* --- Shell: help, ver, clear, crash + RMFS + div + kb --- */
#define SHELL_MAX 128

void vga_print_uint(unsigned int v, unsigned char color) {
    char buf[11];
    int i = 10;
    buf[10] = 0;
    if (v == 0) {
        vga_putchar('0', color);
        return;
    }
    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    vga_print(&buf[i], color);
}

static unsigned int katoi(const char *s) {
    unsigned int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned int)(*s - '0'); s++; }
    return v;
}

static unsigned char cmos_read(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static unsigned char unbcd(unsigned char v) {
    return (unsigned char)(((v >> 4) & 0xF) * 10 + (v & 0xF));
}

static void vga_print2(unsigned int v, unsigned char color) {
    vga_putchar((char)('0' + (v / 10) % 10), color);
    vga_putchar((char)('0' + v % 10), color);
}

/* PC speaker beep via PIT channel 2. Delay loop is uncalibrated. */
static void pc_beep(unsigned int freq) {
    unsigned int div;
    unsigned char s;
    if (freq < 37) freq = 37;
    if (freq > 20000) freq = 20000;
    div = 1193182 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (unsigned char)(div & 0xFF));
    outb(0x42, (unsigned char)((div >> 8) & 0xFF));
    s = inb(0x61);
    outb(0x61, s | 3);
    for (volatile unsigned int i = 0; i < 400000; i++) {
        __asm__ volatile ("pause");
    }
    outb(0x61, (unsigned char)(s & ~3));
}

static void sys_reboot(void) {
    vga_print("rebooting...\n", C_GREY_ON_BLACK);
    outb(0x64, 0xFE); /* keyboard controller: pulse CPU reset */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void shell_cmd_help(void) {
    vga_print("commands:\n", C_GREY_ON_BLACK);
    vga_print("help  - show this list\n", C_GREY_ON_BLACK);
    vga_print("ver   - show EOS version\n", C_GREY_ON_BLACK);
    vga_print("clear - clear screen\n", C_GREY_ON_BLACK);
    vga_print("ls [dir]      - list RMFS files\n", C_GREY_ON_BLACK);
    vga_print("cat <file>    - print RMFS file\n", C_GREY_ON_BLACK);
    vga_print("write <f> <t> - write text t into RMFS file f\n", C_GREY_ON_BLACK);
    vga_print("rm <file>     - delete RMFS file\n", C_GREY_ON_BLACK);
    vga_print("crash - trigger E4 fault (test)\n", C_GREY_ON_BLACK);
    vga_print("div [dir]     - browse RMFS (arrows, enter, shift+enter)\n", C_GREY_ON_BLACK);
    vga_print("kb [us|tr]    - keyboard layout\n", C_GREY_ON_BLACK);
    vga_print("echo <text>   - print text\n", C_GREY_ON_BLACK);
    vga_print("beep [freq]   - PC speaker beep\n", C_GREY_ON_BLACK);
    vga_print("reboot        - reboot the machine\n", C_GREY_ON_BLACK);
    vga_print("mem           - memory + RMFS usage\n", C_GREY_ON_BLACK);
    vga_print("iso           - boot CD info\n", C_GREY_ON_BLACK);
    vga_print("exec <file>   - run an EOS program\n", C_GREY_ON_BLACK);
    vga_print("time          - CMOS real-time clock\n", C_GREY_ON_BLACK);
    vga_print("touch <file>  - create empty file\n", C_GREY_ON_BLACK);
    vga_print("cp <src> <dst>- copy file\n", C_GREY_ON_BLACK);
    vga_print("wc <file>     - count lines and bytes\n", C_GREY_ON_BLACK);
    vga_print("eword         - EWord dictionary window\n", C_GREY_ON_BLACK);
}

/* Split "cmd rest..." in place: NUL-terminates cmd, returns ptr to rest. */
static char *shell_split(char *line) {
    while (*line && *line != ' ') line++;
    if (*line == 0) return line;
    *line = 0;
    line++;
    while (*line == ' ') line++;
    return line;
}

static unsigned int shell_ls_n = 0;

/* Strip div("x") / div "x" leftovers down to a plain RMFS path. */
static void div_strip(char *s) {
    if (s[0] == '(') {
        unsigned int n = kstrlen(s);
        unsigned int i = 1, j = 0;
        if (n >= 2 && s[n - 1] == ')') n--;
        while (i < n) s[j++] = s[i++];
        s[j] = 0;
    }
    {
        /* trailing ')' (shell already split off the leading '(') */
        unsigned int n = kstrlen(s);
        if (n >= 1 && s[n - 1] == ')') s[n - 1] = 0;
    }
    {
        unsigned int n = kstrlen(s);
        if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
            for (unsigned int i = 1; i < n - 1; i++) s[i - 1] = s[i];
            s[n - 2] = 0;
        }
    }
    while (*s == '/') { /* RMFS paths are relative: no leading slashes */
        unsigned int i = 0;
        while (s[i]) { s[i] = s[i + 1]; i++; }
    }
}

static void shell_ls_count(const char *name, unsigned int size) {
    (void)name; (void)size;
    shell_ls_n++;
}

static void shell_ls_one(const char *name, unsigned int size) {
    shell_ls_n++;
    vga_print(name, C_GREY_ON_BLACK);
    vga_print("  ", C_GREY_ON_BLACK);
    vga_print_uint(size, C_GREY_ON_BLACK);
    vga_putchar('\n', C_GREY_ON_BLACK);
}

static char shell_iobuf[RMFS_MAX_SIZE + 1]; /* .bss: cat staging */

static void shell(void) {
    static char line[SHELL_MAX];

    for (;;) {
        vga_print("eos> ", C_CYAN_ON_BLACK);
        unsigned int len = 0;
        for (;;) {
            char c = kbd_getchar();
            if (c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT)
                continue; /* reserved for the div browser (v0.0.5) */
            if (c == '\n') {
                vga_putchar('\n', C_GREY_ON_BLACK);
                break;
            } else if (c == '\b') {
                if (len > 0) {
                    len--;
                    vga_backspace();
                }
            } else if (len < SHELL_MAX - 1) {
                line[len++] = c;
                vga_putchar(c, C_GREY_ON_BLACK);
            }
        }
        line[len] = 0;
        if (len == 0)
            continue;
        char *rest;
        if (kstrncmp(line, "div", 3) == 0 &&
            (line[3] == 0 || line[3] == ' ' || line[3] == '(')) {
            int paren = (line[3] == '(');
            line[3] = 0;
            rest = line + 3 + (paren ? 1 : 0);
            while (*rest == ' ') rest++;
        } else {
            rest = shell_split(line);
        }
        if (kstrcmp(line, "help") == 0) {
            shell_cmd_help();
        } else if (kstrcmp(line, "ver") == 0) {
            vga_print(EOS_VER " - GPLv3 hobby OS\n", C_GREY_ON_BLACK);
        } else if (kstrcmp(line, "clear") == 0) {
            vga_clear();
        } else if (kstrcmp(line, "crash") == 0) {
            __asm__ volatile ("ud2"); /* invalid opcode -> EOS ERR E4 via IDT */
        } else if (kstrcmp(line, "exec") == 0) {
            /* Run a flat EOS program from RMFS at 0x20000 ("EOS" magic).
               Callee preserves EBX ESI EDI EBP, prints on rows 10+. */
            if (*rest == 0) {
                vga_print("usage: exec <file>\n", C_RED_ON_BLACK);
            } else {
                int n = rmfs_read(rest, (char *)0x20000, RMFS_MAX_SIZE);
                if (n < 4) {
                    vga_print("no such file: ", C_RED_ON_BLACK);
                    vga_print(rest, C_RED_ON_BLACK);
                    vga_putchar('\n', C_RED_ON_BLACK);
                } else if (*(unsigned int *)0x20000 != 0x21534F45) {
                    vga_print("bad magic (not an EOS program)\n", C_RED_ON_BLACK);
                } else {
                    ((void (*)(void))0x20004)();
                }
            }
        } else if (kstrcmp(line, "time") == 0) {
            unsigned char st = cmos_read(0x0B);
            unsigned char s = cmos_read(0x00), m = cmos_read(0x02);
            unsigned char h = cmos_read(0x04), day = cmos_read(0x07);
            unsigned char mon = cmos_read(0x08), yr = cmos_read(0x09);
            if (!(st & 4)) {
                s = unbcd(s); m = unbcd(m); h = unbcd(h);
                day = unbcd(day); mon = unbcd(mon); yr = unbcd(yr);
            }
            vga_print2(h, C_GREY_ON_BLACK);
            vga_putchar(':', C_GREY_ON_BLACK);
            vga_print2(m, C_GREY_ON_BLACK);
            vga_putchar(':', C_GREY_ON_BLACK);
            vga_print2(s, C_GREY_ON_BLACK);
            vga_putchar(' ', C_GREY_ON_BLACK);
            vga_print("20", C_GREY_ON_BLACK);
            vga_print2(yr, C_GREY_ON_BLACK);
            vga_putchar('-', C_GREY_ON_BLACK);
            vga_print2(mon, C_GREY_ON_BLACK);
            vga_putchar('-', C_GREY_ON_BLACK);
            vga_print2(day, C_GREY_ON_BLACK);
            vga_putchar('\n', C_GREY_ON_BLACK);
        } else if (kstrcmp(line, "touch") == 0) {
            if (*rest == 0) {
                vga_print("usage: touch <file>\n", C_RED_ON_BLACK);
            } else if (rmfs_read(rest, shell_iobuf, 0) >= 0) {
                vga_print("already exists\n", C_GREY_ON_BLACK);
            } else if (rmfs_write(rest, "", 0) == 0) {
                vga_print("created ", C_GREY_ON_BLACK);
                vga_print(rest, C_GREY_ON_BLACK);
                vga_putchar('\n', C_GREY_ON_BLACK);
            } else {
                vga_print("bad file name\n", C_RED_ON_BLACK);
            }
        } else if (kstrcmp(line, "cp") == 0) {
            char *src = rest;
            char *dst = shell_split(rest);
            if (*src == 0 || *dst == 0) {
                vga_print("usage: cp <src> <dst>\n", C_RED_ON_BLACK);
            } else {
                int n = rmfs_read(src, shell_iobuf, RMFS_MAX_SIZE);
                if (n < 0) {
                    vga_print("no such file: ", C_RED_ON_BLACK);
                    vga_print(src, C_RED_ON_BLACK);
                    vga_putchar('\n', C_RED_ON_BLACK);
                } else if (rmfs_write(dst, shell_iobuf, (unsigned int)n) == 0) {
                    vga_print("copied ", C_GREY_ON_BLACK);
                    vga_print_uint((unsigned int)n, C_GREY_ON_BLACK);
                    vga_print(" bytes\n", C_GREY_ON_BLACK);
                } else {
                    vga_print("copy failed (full? bad name?)\n", C_RED_ON_BLACK);
                }
            }
        } else if (kstrcmp(line, "wc") == 0) {
            if (*rest == 0) {
                vga_print("usage: wc <file>\n", C_RED_ON_BLACK);
            } else {
                int n = rmfs_read(rest, shell_iobuf, RMFS_MAX_SIZE);
                if (n < 0) {
                    vga_print("no such file: ", C_RED_ON_BLACK);
                    vga_print(rest, C_RED_ON_BLACK);
                    vga_putchar('\n', C_RED_ON_BLACK);
                } else {
                    unsigned int lines = 0;
                    for (int i = 0; i < n; i++)
                        if (shell_iobuf[i] == '\n') lines++;
                    vga_print_uint(lines, C_GREY_ON_BLACK);
                    vga_putchar(' ', C_GREY_ON_BLACK);
                    vga_print_uint((unsigned int)n, C_GREY_ON_BLACK);
                    vga_putchar(' ', C_GREY_ON_BLACK);
                    vga_print(rest, C_GREY_ON_BLACK);
                    vga_putchar('\n', C_GREY_ON_BLACK);
                }
            }
        } else if (kstrcmp(line, "eword") == 0) {
            eword_open();
            vga_clear();
        } else if (kstrcmp(line, "div") == 0) {
            div_strip(rest);
            if (*rest == 0) {
                div_browse("");
            } else {
                shell_ls_n = 0;
                rmfs_ls_dir(rest, shell_ls_count);
                if (shell_ls_n > 0) {
                    div_browse(rest);
                } else {
                    int n = rmfs_read(rest, shell_iobuf, RMFS_MAX_SIZE);
                    if (n >= 0)
                        div_view(rest);
                    else {
                        vga_print("no such folder: ", C_RED_ON_BLACK);
                        vga_print(rest, C_RED_ON_BLACK);
                        vga_putchar('\n', C_RED_ON_BLACK);
                    }
                }
            }
            vga_clear();
        } else if (kstrcmp(line, "kb") == 0) {
            if (*rest == 0) {
                vga_print(kbd_layout_is_tr() ? "keyboard: tr\n" : "keyboard: us\n",
                          C_GREY_ON_BLACK);
            } else if (kstrcmp(rest, "tr") == 0) {
                kbd_set_layout_tr(1);
                vga_print("keyboard: tr (Turkish-Q)\n", C_GREY_ON_BLACK);
            } else if (kstrcmp(rest, "us") == 0) {
                kbd_set_layout_tr(0);
                vga_print("keyboard: us\n", C_GREY_ON_BLACK);
            } else {
                vga_print("usage: kb [us|tr]\n", C_RED_ON_BLACK);
            }
        } else if (kstrcmp(line, "echo") == 0) {
            vga_print(rest, C_GREY_ON_BLACK);
            vga_putchar('\n', C_GREY_ON_BLACK);
        } else if (kstrcmp(line, "beep") == 0) {
            pc_beep(*rest ? katoi(rest) : 440);
        } else if (kstrcmp(line, "reboot") == 0) {
            sys_reboot();
        } else if (kstrcmp(line, "mem") == 0) {
            unsigned int files = 0, bytes = 0;
            rmfs_stat(&files, &bytes);
            vga_print("RMFS: ", C_GREY_ON_BLACK);
            vga_print_uint(files, C_GREY_ON_BLACK);
            vga_print(" files, ", C_GREY_ON_BLACK);
            vga_print_uint(bytes, C_GREY_ON_BLACK);
            vga_print(" / 65536 bytes\n", C_GREY_ON_BLACK);
            vga_print("kernel @0x1000  stack @0x90000  vga @0xB8000\n",
                      C_GREY_ON_BLACK);
        } else if (kstrcmp(line, "iso") == 0) {
            vga_print("boot drive: ", C_GREY_ON_BLACK);
            vga_print_hex(boot_drive, C_GREY_ON_BLACK);
            if (boot_drive == 0xE0 || boot_drive == 0xE1)
                vga_print(" (El Torito CD)\n", C_GREY_ON_BLACK);
            else if (boot_drive < 0x80)
                vga_print(" (floppy)\n", C_GREY_ON_BLACK);
            else
                vga_print(" (hard disk)\n", C_GREY_ON_BLACK);
            vga_print("boot image: no-emulation, 72 sectors\n", C_GREY_ON_BLACK);
            vga_print("ISO9660: BOOT.BIN KERNEL.BIN LICENSE README.TXT\n",
                      C_GREY_ON_BLACK);
        } else if (kstrcmp(line, "rmfsdump") == 0) {
            vga_print("RMFS table:\n", C_CYAN_ON_BLACK);
            rmfs_debug_dump();
        } else if (kstrcmp(line, "ls") == 0) {
            shell_ls_n = 0;
            if (*rest == 0)
                rmfs_ls(shell_ls_one);
            else
                rmfs_ls_dir(rest, shell_ls_one);
            if (shell_ls_n == 0)
                vga_print("(empty)\n", C_GREY_ON_BLACK);
        } else if (kstrcmp(line, "cat") == 0) {
            if (*rest == 0) {
                vga_print("usage: cat <file>\n", C_RED_ON_BLACK);
            } else {
                int n = rmfs_read(rest, shell_iobuf, RMFS_MAX_SIZE);
                if (n < 0) {
                    vga_print("no such file: ", C_RED_ON_BLACK);
                    vga_print(rest, C_RED_ON_BLACK);
                    vga_putchar('\n', C_RED_ON_BLACK);
                } else {
                    shell_iobuf[n] = 0;
                    vga_print(shell_iobuf, C_GREY_ON_BLACK);
                    vga_putchar('\n', C_GREY_ON_BLACK);
                }
            }
        } else if (kstrcmp(line, "write") == 0) {
            char *name = rest;
            char *content = shell_split(rest);
            if (*name == 0) {
                vga_print("usage: write <file> <text>\n", C_RED_ON_BLACK);
            } else {
                int rc = rmfs_write(name, content, kstrlen(content));
                if (rc == 0) {
                    vga_print("wrote ", C_GREY_ON_BLACK);
                    vga_print_uint(kstrlen(content), C_GREY_ON_BLACK);
                    vga_print(" bytes to ", C_GREY_ON_BLACK);
                    vga_print(name, C_GREY_ON_BLACK);
                    vga_putchar('\n', C_GREY_ON_BLACK);
                } else if (rc == -1) {
                    vga_print("RMFS full (16 files max)\n", C_RED_ON_BLACK);
                } else if (rc == -2) {
                    vga_print("file too big (4KB max)\n", C_RED_ON_BLACK);
                } else {
                    vga_print("bad file name\n", C_RED_ON_BLACK);
                }
            }
        } else if (kstrcmp(line, "rm") == 0) {
            if (*rest == 0) {
                vga_print("usage: rm <file>\n", C_RED_ON_BLACK);
            } else if (rmfs_rm(rest) == 0) {
                vga_print("deleted ", C_GREY_ON_BLACK);
                vga_print(rest, C_GREY_ON_BLACK);
                vga_putchar('\n', C_GREY_ON_BLACK);
            } else {
                vga_print("no such file: ", C_RED_ON_BLACK);
                vga_print(rest, C_RED_ON_BLACK);
                vga_putchar('\n', C_RED_ON_BLACK);
            }
        } else {
            vga_print("unknown command: ", C_RED_ON_BLACK);
            vga_print(line, C_RED_ON_BLACK);
            if (*rest) {
                vga_putchar(' ', C_RED_ON_BLACK);
                vga_print(rest, C_RED_ON_BLACK);
            }
            vga_putchar('\n', C_RED_ON_BLACK);
        }
    }
}

static void eos_selftest(void) {
    /* Minimal sanity checks; any failure -> one of the 5 errors. */
    if ((unsigned int)vga != VGA_ADDR)
        eos_panic(EOS_E3_VGA);
    if (kstrlen("EOS") != 3)
        eos_panic(EOS_E4_EXCEPTION);
}

void kernel_main(void) {
    eos_selftest();
    idt_install();
    boot_drive = 0xFF;
    __asm__ volatile ("movb %1, %0" : "=r"(boot_drive) : "m"(*(volatile unsigned char *)0x500));

    /* Bootloader already showed "EOS loading..." on row 0. */
    cursor_row = 1;
    cursor_col = 0;
    vga_print("hello world!", C_GREEN_ON_BLACK);
    vga_putchar('\n', C_GREEN_ON_BLACK);
    vga_print(EOS_VER " - shell inside. Type 'help'.", C_GREY_ON_BLACK);
    vga_putchar('\n', C_GREY_ON_BLACK);

    rmfs_init();
    rmfs_write("eos/test.txt", "hi from RMFS", 12);
    rmfs_write("eos/readme.txt", "RMFS keeps files in RAM.", 24);
    rmfs_write("hello.txt", "hello world!", 12);
    rmfs_write("demo", (const char *)demo_prog, demo_prog_len);
    mouse_init(); /* PIC remap + PS/2 mouse + sti (continues without one) */

    shell(); /* never returns */

    eos_panic(EOS_E5_HALT);
}
