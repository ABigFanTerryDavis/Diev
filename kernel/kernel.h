/* Diev OS - shared kernel API.
 * Copyright (C) 2026 Diev contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#ifndef DIEV_KERNEL_H
#define DIEV_KERNEL_H

/* VGA colors */
#define C_GREY_ON_BLACK 0x07
#define C_GREEN_ON_BLACK 0x0A
#define C_RED_ON_BLACK 0x0C
#define C_CYAN_ON_BLACK 0x0B
#define C_YELLOW_ON_BLACK 0x0E
#define C_WHITE_ON_BLACK 0x0F

/* Extended/special keys (codes above ASCII; shared with div.c). */
#define KEY_UP    ((char)0x90)
#define KEY_DOWN  ((char)0x91)
#define KEY_LEFT  ((char)0x92)
#define KEY_RIGHT ((char)0x93)
#define K_F2      ((char)0x95)
#define K_ESC     ((char)0x9B)

/* --- Diev 5 errors --- */
typedef enum {
    DIEV_E1_DISK = 1,     /* boot disk / kernel load failure */
    DIEV_E2_KERNEL = 2,   /* kernel integrity / entry failure */
    DIEV_E3_VGA = 3,      /* VGA / display fault */
    DIEV_E4_EXCEPTION = 4,/* unknown exception / unexpected state */
    DIEV_E5_HALT = 5      /* unrecoverable halt fault */
} diev_error_t;

void vga_putchar(char c, unsigned char color);
void vga_print(const char *s, unsigned char color);
void vga_print_uint(unsigned int v, unsigned char color);
void vga_print_hex(unsigned int v, unsigned char color);
void vga_clear(void);
void vga_set_cursor(unsigned char row, unsigned char col);
void diev_panic(diev_error_t code);

/* Keyboard layouts (us default, tr = Turkish-Q) + raw reader for div. */
void kbd_set_layout_tr(int tr);
int kbd_layout_is_tr(void);
char div_getkey(int *shift_state);

#endif
