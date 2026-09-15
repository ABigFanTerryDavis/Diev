/* EOS - PS/2 mouse driver (IRQ12, polling-free).
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#ifndef EOS_MOUSE_H
#define EOS_MOUSE_H

/* init: remap PIC, unmask cascade+mouse, enable packets, sti.
 * Returns 1 if a mouse answers, 0 otherwise (safe to ignore). */
int mouse_init(void);
/* Called from irq_handler on vector 0x2C. Reads one packet byte. */
void mouse_irq(void);
int mouse_present(void);
int mouse_x(void);
int mouse_y(void);
int mouse_left(void);   /* left button currently held */
void mouse_draw(void);  /* paint cursor block at current pos */
void mouse_hide(void);  /* restore cell under cursor */

#endif
