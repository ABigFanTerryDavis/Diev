/* EOS - tiny text-mode window manager (save/restore, drag).
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 *
 * Windows are screen regions with a border + title. The shell keeps
 * working underneath: closing a window restores every cell. Dragging
 * follows the mouse while the left button is held on the title bar.
 */
#ifndef EOS_WIN_H
#define EOS_WIN_H

#define WIN_MAX 3
#define WIN_MAXW 62
#define WIN_MAXH 20

/* Open a window; returns slot 0..WIN_MAX-1, or -1 if full. */
int win_open(const char *title, int w, int h, int row, int col);
/* Close (restore cells). Slots above shift down. */
void win_close(int id);
/* Redraw border + title (call after moving or content changes). */
void win_frame(int id);
/* Move window; follows clamping. Redraws frame. */
void win_move(int id, int row, int col);
/* Geometry accessors for content drawing. */
int win_row(int id);
int win_col(int id);
int win_w(int id);
int win_h(int id);
int win_count(void);
/* Track mouse drag on the top window's title bar. Call each UI loop. */
void win_drag_update(void);

#endif
