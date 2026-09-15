/* EOS - window manager implementation.
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#include "win.h"
#include "kernel.h"
#include "mouse.h"

#define VGA ((volatile unsigned short *)0xB8000)
#define COLS 80
#define ROWS 25

typedef struct {
    int used;
    char title[32];
    int w, h, row, col;
    unsigned short save[WIN_MAXH + 2][WIN_MAXW + 2];
    int dragging;
} win_t;

static win_t wins[WIN_MAX];
static unsigned int nwins = 0;

static void cell(int r, int c, char ch, unsigned char attr) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    VGA[r * COLS + c] = (unsigned short)ch | ((unsigned short)attr << 8);
}

static void clamp_pos(int *row, int *col, int w, int h) {
    if (*row < 0) *row = 0;
    if (*col < 0) *col = 0;
    if (*row + h + 1 >= ROWS) *row = ROWS - h - 2;
    if (*col + w + 1 >= COLS) *col = COLS - w - 2;
    if (*row < 0) *row = 0;
    if (*col < 0) *col = 0;
}

int win_open(const char *title, int w, int h, int row, int col) {
    int id = -1;
    for (int i = 0; i < WIN_MAX; i++) {
        if (!wins[i].used) { id = i; break; }
    }
    if (id < 0) return -1;
    if (w > WIN_MAXW) w = WIN_MAXW;
    if (h > WIN_MAXH) h = WIN_MAXH;
    clamp_pos(&row, &col, w, h);
    mouse_hide();
    for (int r = 0; r <= h + 1; r++)
        for (int c = 0; c <= w + 1; c++)
            wins[id].save[r][c] = VGA[(row + r) * COLS + (col + c)];
    wins[id].used = 1;
    wins[id].w = w;
    wins[id].h = h;
    wins[id].row = row;
    wins[id].col = col;
    wins[id].dragging = 0;
    {
        int i = 0;
        while (i < 31 && title[i]) { wins[id].title[i] = title[i]; i++; }
        wins[id].title[i] = 0;
    }
    nwins++;
    win_frame(id);
    mouse_draw();
    return id;
}

void win_frame(int id) {
    int r, c;
    int row = wins[id].row, col = wins[id].col;
    int w = wins[id].w, h = wins[id].h;
    mouse_hide();
    cell(row, col, (char)0xDA, 0x0B);
    for (c = 1; c <= w; c++) cell(row, col + c, (char)0xC4, 0x0B);
    cell(row, col + w + 1, (char)0xBF, 0x0B);
    for (r = 1; r <= h; r++) {
        cell(row + r, col, (char)0xB3, 0x0B);
        cell(row + r, col + w + 1, (char)0xB3, 0x0B);
    }
    cell(row + h + 1, col, (char)0xC0, 0x0B);
    for (c = 1; c <= w; c++) cell(row + h + 1, col + c, (char)0xC4, 0x0B);
    cell(row + h + 1, col + w + 1, (char)0xD9, 0x0B);
    /* title, clipped */
    for (c = 0; c < w - 1 && wins[id].title[c]; c++)
        cell(row, col + 2 + c, wins[id].title[c], 0x0B);
    mouse_draw();
}

void win_move(int id, int row, int col) {
    int r, c;
    int orow, ocol;
    if (id < 0 || id >= WIN_MAX || !wins[id].used) return;
    clamp_pos(&row, &col, wins[id].w, wins[id].h);
    mouse_hide();
    /* restore old cells */
    orow = wins[id].row;
    ocol = wins[id].col;
    for (r = 0; r <= wins[id].h + 1; r++)
        for (c = 0; c <= wins[id].w + 1; c++)
            VGA[(orow + r) * COLS + (ocol + c)] = wins[id].save[r][c];
    /* save new cells */
    for (r = 0; r <= wins[id].h + 1; r++)
        for (c = 0; c <= wins[id].w + 1; c++)
            wins[id].save[r][c] = VGA[(row + r) * COLS + (col + c)];
    wins[id].row = row;
    wins[id].col = col;
    win_frame(id);
    mouse_draw();
}

void win_close(int id) {
    int r, c;
    if (id < 0 || id >= WIN_MAX || !wins[id].used) return;
    mouse_hide();
    for (r = 0; r <= wins[id].h + 1; r++)
        for (c = 0; c <= wins[id].w + 1; c++)
            VGA[(wins[id].row + r) * COLS + (wins[id].col + c)] =
                wins[id].save[r][c];
    wins[id].used = 0;
    wins[id].dragging = 0;
    nwins--;
    mouse_draw();
}

int win_row(int id) { return wins[id].row; }
int win_col(int id) { return wins[id].col; }
int win_w(int id) { return wins[id].w; }
int win_h(int id) { return wins[id].h; }
int win_count(void) { return (int)nwins; }

void win_drag_update(void) {
    int id;
    /* topmost used window */
    for (id = WIN_MAX - 1; id >= 0; id--)
        if (wins[id].used) break;
    if (id < 0) return;
    if (!mouse_present()) return;
    {
        int mx = mouse_x(), my = mouse_y();
        int on_title = (my == wins[id].row) &&
                       (mx >= wins[id].col) &&
                       (mx <= wins[id].col + wins[id].w + 1);
        if (mouse_left() && (wins[id].dragging || on_title)) {
            if (!wins[id].dragging) wins[id].dragging = 1;
            /* follow: keep grab offset stable via top-left follow */
            win_move(id, my, mx - 2);
        } else {
            wins[id].dragging = 0;
        }
    }
}
