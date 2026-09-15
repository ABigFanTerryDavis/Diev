/* Diev OS - div browser, viewer and editor implementation.
 * Copyright (C) 2026 Diev contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#include "div.h"
#include "kernel.h"
#include "rmfs.h"

#define DIV_MAX_ENTRIES 32
#define DIV_MAX_PATH 64
#define DIV_ROWS 20       /* visible entries per page */
#define DIV_TOP_ROW 2     /* first entry row on screen */

typedef struct {
    char name[RMFS_MAX_NAME];
    char path[DIV_MAX_PATH];
    int is_dir;
    unsigned int size;
} div_entry_t;

static div_entry_t div_entries[DIV_MAX_ENTRIES];
static unsigned int div_n;

/* --- tiny string helpers (no libc) --- */
static unsigned int dlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int dcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int dstarts(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static void dcopy(char *dst, const char *src, unsigned int max) {
    unsigned int i = 0;
    while (i + 1 < max && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* --- entry collection --- */
static char div_dir[DIV_MAX_PATH]; /* "" = root, else "diev" (no trailing /) */

static void div_add(const char *name, const char *path, int is_dir, unsigned int size) {
    if (is_dir) {
        for (unsigned int i = 0; i < div_n; i++)
            if (div_entries[i].is_dir && dcmp(div_entries[i].name, name) == 0)
                return; /* dedupe folders */
    }
    if (div_n >= DIV_MAX_ENTRIES) return;
    dcopy(div_entries[div_n].name, name, RMFS_MAX_NAME);
    dcopy(div_entries[div_n].path, path, DIV_MAX_PATH);
    div_entries[div_n].is_dir = is_dir;
    div_entries[div_n].size = size;
    div_n++;
}

static void div_collect_one(const char *name, unsigned int size) {
    /* name = full RMFS path, e.g. "diev/test.txt" */
    unsigned int dl = dlen(div_dir);
    if (dl == 0) {
        /* root: bare files show directly, "x/..." becomes folder "x" */
        unsigned int i = 0;
        while (name[i] && name[i] != '/') i++;
        if (name[i] == 0) {
            div_add(name, name, 0, size);
        } else {
            char folder[RMFS_MAX_NAME];
            unsigned int k = 0;
            while (k + 1 < RMFS_MAX_NAME && k < i) { folder[k] = name[k]; k++; }
            folder[k] = 0;
            div_add(folder, folder, 1, 0);
        }
        return;
    }
    if (!dstarts(name, div_dir) || name[dl] != '/') return;
    {
        const char *rest = name + dl + 1;
        unsigned int i = 0;
        char first[DIV_MAX_PATH];
        unsigned int fl = dlen(div_dir);
        unsigned int p = 0;
        while (p + 1 < DIV_MAX_PATH && p < fl) { first[p] = div_dir[p]; p++; }
        first[p++] = '/';
        while (rest[i] && rest[i] != '/' && p + 1 < DIV_MAX_PATH) {
            first[p++] = rest[i++];
        }
        first[p] = 0;
        if (rest[i] == 0) {
            div_add(rest, name, 0, size);          /* plain file */
        } else if (rest[i] == '/') {
            char folder[RMFS_MAX_NAME];
            unsigned int k = 0;
            while (rest[k] && rest[k] != '/' && k + 1 < RMFS_MAX_NAME) {
                folder[k] = rest[k]; k++;
            }
            folder[k] = 0;
            div_add(folder, first, 1, 0);          /* subfolder */
        }
    }
}

static void div_collect(const char *dir) {
    dcopy(div_dir, dir, DIV_MAX_PATH);
    div_n = 0;
    rmfs_ls(div_collect_one);
}

/* --- browser drawing --- */
static void div_draw(unsigned int sel, unsigned int top) {
    vga_clear();
    vga_set_cursor(0, 0);
    vga_print("RMFS div - /", C_CYAN_ON_BLACK);
    vga_print(div_dir, C_CYAN_ON_BLACK);
    vga_putchar('\n', C_CYAN_ON_BLACK);
    vga_putchar('\n', C_CYAN_ON_BLACK);
    if (div_n == 0) {
        vga_print("(empty)\n", C_GREY_ON_BLACK);
    }
    for (unsigned int r = 0; r < DIV_ROWS; r++) {
        unsigned int i = top + r;
        if (i >= div_n) break;
        vga_set_cursor((unsigned char)(DIV_TOP_ROW + r), 0);
        if (i == sel) {
            vga_print("> ", C_WHITE_ON_BLACK);
            vga_print(div_entries[i].name, C_WHITE_ON_BLACK);
        } else {
            vga_print("  ", C_GREY_ON_BLACK);
            vga_print(div_entries[i].name,
                      div_entries[i].is_dir ? C_CYAN_ON_BLACK : C_YELLOW_ON_BLACK);
        }
        if (div_entries[i].is_dir)
            vga_print("/", C_GREY_ON_BLACK);
    }
    vga_set_cursor(23, 0);
    vga_print("up/down move  enter open  shift+enter edit  esc shell",
              C_GREY_ON_BLACK);
    vga_set_cursor(24, 79); /* park the cursor out of the way */
}

/* --- viewer: read-only fullscreen cat --- */
static char div_view_buf[RMFS_MAX_SIZE + 1];

void div_view(const char *path) {
    int n = rmfs_read(path, div_view_buf, RMFS_MAX_SIZE);
    vga_clear();
    vga_set_cursor(0, 0);
    vga_print(path, C_YELLOW_ON_BLACK);
    vga_putchar('\n', C_YELLOW_ON_BLACK);
    vga_putchar('\n', C_YELLOW_ON_BLACK);
    if (n < 0) {
        vga_print("no such file\n", C_RED_ON_BLACK);
    } else {
        unsigned int rows = 0;
        div_view_buf[n] = 0;
        for (int i = 0; i < n && rows < 20; i++) {
            if (div_view_buf[i] == '\n') {
                rows++;
                if (rows >= 20) {
                    vga_print("\n...(more)", C_GREY_ON_BLACK);
                    break;
                }
            }
            vga_putchar(div_view_buf[i], C_GREY_ON_BLACK);
        }
    }
    vga_set_cursor(24, 0);
    vga_print("esc back", C_GREY_ON_BLACK);
    vga_set_cursor(24, 79);
    for (;;) {
        int sh = 0;
        if (div_getkey(&sh) == K_ESC) return;
    }
}

/* --- editor: fullscreen RMFS writer --- */
static char ed_buf[RMFS_MAX_SIZE + 1];
static unsigned int ed_len, ed_pos, ed_top;
static unsigned int ed_goal; /* remembered column for up/down */

static void ed_move(char *dst, const char *src, unsigned int n) {
    if (dst < src) {
        for (unsigned int i = 0; i < n; i++) dst[i] = src[i];
    } else {
        for (unsigned int i = n; i > 0; i--) dst[i - 1] = src[i - 1];
    }
}

static unsigned int ed_line_start(unsigned int pos) {
    while (pos > 0 && ed_buf[pos - 1] != '\n') pos--;
    return pos;
}

static unsigned int ed_col(unsigned int pos) {
    return pos - ed_line_start(pos);
}

static unsigned int ed_line_no(unsigned int pos) {
    unsigned int line = 0;
    for (unsigned int i = 0; i < pos; i++)
        if (ed_buf[i] == '\n') line++;
    return line;
}

static unsigned int ed_pos_of(unsigned int line, unsigned int col) {
    unsigned int pos = 0, cur = 0;
    while (pos < ed_len && cur < line) {
        if (ed_buf[pos] == '\n') cur++;
        pos++;
    }
    while (pos < ed_len && ed_buf[pos] != '\n' && col > 0) {
        pos++; col--;
    }
    return pos;
}

#define ED_TOP_ROW 2
#define ED_ROWS 21      /* rows 2..22, status on 24 */

static void ed_draw(const char *path, const char *status) {
    unsigned int line = ed_line_no(ed_pos);
    if (line < ed_top) ed_top = line;
    if (line >= ed_top + ED_ROWS) ed_top = line - ED_ROWS + 1;
    vga_clear();
    vga_set_cursor(0, 0);
    vga_print("RMFS edit - ", C_CYAN_ON_BLACK);
    vga_print(path, C_CYAN_ON_BLACK);
    vga_putchar('\n', C_CYAN_ON_BLACK);
    vga_print("f2 save   esc quit (no save)", C_GREY_ON_BLACK);
    vga_putchar('\n', C_GREY_ON_BLACK);
    {
        unsigned int pos = ed_pos_of(ed_top, 0);
        unsigned int row = 0;
        while (pos < ed_len && row < ED_ROWS) {
            if (ed_buf[pos] == '\n') {
                row++;
                if (row >= ED_ROWS) break;
                vga_putchar('\n', C_GREY_ON_BLACK);
                pos++;
                continue;
            }
            vga_putchar(ed_buf[pos], C_GREY_ON_BLACK);
            pos++;
        }
    }
    vga_set_cursor(24, 0);
    if (status) vga_print(status, C_YELLOW_ON_BLACK);
    vga_set_cursor((unsigned char)(ED_TOP_ROW + (ed_line_no(ed_pos) - ed_top)),
                   (unsigned char)ed_col(ed_pos));
}

static void editor_open(const char *path) {
    int n = rmfs_read(path, ed_buf, RMFS_MAX_SIZE);
    if (n < 0) n = 0;
    ed_len = (unsigned int)n;
    ed_pos = ed_len;
    ed_top = 0;
    ed_goal = 0;
    ed_draw(path, 0);
    for (;;) {
        int sh = 0;
        char k = div_getkey(&sh);
        if (k == K_ESC) {
            return; /* quit without saving */
        } else if (k == K_F2) {
            int rc = rmfs_write(path, ed_buf, ed_len);
            if (rc == 0) return; /* saved: back to browser */
            ed_draw(path, "save failed (RMFS full?)");
        } else if (k == '\b') {
            if (ed_pos > 0) {
                ed_pos--;
                ed_move(ed_buf + ed_pos, ed_buf + ed_pos + 1, ed_len - ed_pos - 1);
                ed_len--;
                ed_goal = ed_col(ed_pos);
                ed_draw(path, 0);
            }
        } else if (k == KEY_LEFT) {
            if (ed_pos > 0) {
                ed_pos--;
                ed_goal = ed_col(ed_pos);
                ed_draw(path, 0);
            }
        } else if (k == KEY_RIGHT) {
            if (ed_pos < ed_len) {
                ed_pos++;
                ed_goal = ed_col(ed_pos);
                ed_draw(path, 0);
            }
        } else if (k == KEY_UP) {
            unsigned int line = ed_line_no(ed_pos);
            if (line > 0) {
                ed_pos = ed_pos_of(line - 1, ed_goal);
                ed_draw(path, 0);
            }
        } else if (k == KEY_DOWN) {
            ed_pos = ed_pos_of(ed_line_no(ed_pos) + 1, ed_goal);
            ed_draw(path, 0);
        } else if (k == '\n' || (k >= 32 && k <= 126) || (unsigned char)k >= 128) {
            if (ed_len < RMFS_MAX_SIZE - 1) {
                ed_move(ed_buf + ed_pos + 1, ed_buf + ed_pos, ed_len - ed_pos);
                ed_buf[ed_pos] = k;
                ed_len++;
                ed_pos++;
                ed_goal = ed_col(ed_pos);
                ed_draw(path, 0);
            }
        }
    }
}

/* --- browser main loop --- */
static void div_enter_parent(char *cur) {
    unsigned int n = dlen(cur);
    while (n > 0 && cur[n - 1] != '/') n--;
    if (n > 0) n--; /* drop the slash too */
    cur[n] = 0;
}

void div_browse(const char *start_dir) {
    static char cur[DIV_MAX_PATH];
    dcopy(cur, start_dir, DIV_MAX_PATH);
    /* strip one trailing slash (rmfs paths never start with one) */
    {
        unsigned int n = dlen(cur);
        while (n > 0 && cur[n - 1] == '/') { n--; cur[n] = 0; }
    }
    for (;;) {
        unsigned int sel = 0, top = 0;
        div_collect(cur);
        for (;;) {
            div_draw(sel, top);
            int sh = 0;
            char k = div_getkey(&sh);
            if (k == KEY_UP) {
                if (sel > 0) {
                    sel--;
                    if (sel < top) top = sel;
                }
            } else if (k == KEY_DOWN) {
                if (sel + 1 < div_n) {
                    sel++;
                    if (sel >= top + DIV_ROWS) top = sel - DIV_ROWS + 1;
                }
            } else if (k == KEY_LEFT) {
                if (cur[0] == 0) return; /* root: back to shell */
                div_enter_parent(cur);
                break; /* rebuild listing */
            } else if (k == KEY_RIGHT || k == '\n') {
                if (div_n == 0) continue;
                if (div_entries[sel].is_dir) {
                    dcopy(cur, div_entries[sel].path, DIV_MAX_PATH);
                    break; /* rebuild listing */
                } else if (sh) {
                    editor_open(div_entries[sel].path);
                    break; /* refresh after edit */
                } else {
                    div_view(div_entries[sel].path);
                    break; /* refresh after view */
                }
            } else if (k == K_ESC) {
                return;
            }
        }
    }
}
