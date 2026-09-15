/* EOS - EWord: 10-word dictionary in a draggable window.
 * Copyright (C) 2026 EOS contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 *
 * Type a prefix, matches list themselves numbered. Tab arms a number,
 * 1-9 completes it, Enter takes the first match, Esc closes.
 */
#include "eword.h"
#include "kernel.h"
#include "win.h"
#include "mouse.h"

#define EW_MAXW 10
#define EW_MAXLEN 16

static const char *ew_words[EW_MAXW] = {
    "hello", "help", "exec", "edit", "clear",
    "div", "reboot", "write", "time", "touch",
};

static const char *ew_means[EW_MAXW] = {
    "a greeting from EOS",
    "lists shell commands",
    "runs an EOS program",
    "change text in files",
    "clears the screen",
    "file browser",
    "restarts EOS",
    "writes text to files",
    "shows the clock",
    "makes empty files",
};

static int ew_starts(const char *s, const char *pre) {
    while (*pre) {
        if (*s != *pre) return 0;
        s++; pre++;
    }
    return 1;
}

static unsigned int ew_len(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

void eword_open(void) {
    static char input[EW_MAXLEN + 1];
    static char meaning[48];
    int id;
    unsigned int ilen = 0;
    int tab_armed = 0;
    int i;

    input[0] = 0;
    meaning[0] = 0;
    id = win_open("EWord", 44, 12, 5, 18);
    if (id < 0) {
        vga_print("no window slots\n", C_RED_ON_BLACK);
        return;
    }
    for (;;) {
        int r0 = win_row(id), c0 = win_col(id);
        int shown = 0;
        int sh = 0;
        char k;
        int rr, cc;
        /* clear content area (rows 1..11) so short redraws leave nothing */
        for (rr = 1; rr <= 11; rr++)
            for (cc = 1; cc <= 43; cc++) {
                vga_set_cursor((unsigned char)(r0 + rr),
                               (unsigned char)(c0 + cc));
                vga_putchar(' ', C_GREY_ON_BLACK);
            }
        /* input line */
        vga_set_cursor((unsigned char)(r0 + 1), (unsigned char)(c0 + 2));
        vga_print("word: ", C_GREY_ON_BLACK);
        vga_print(input, C_YELLOW_ON_BLACK);
        vga_print(" ", C_GREY_ON_BLACK);
        /* matches */
        for (i = 0; i < EW_MAXW && shown < 8; i++) {
            if (ilen == 0 || ew_starts(ew_words[i], input)) {
                vga_set_cursor((unsigned char)(r0 + 2 + shown),
                               (unsigned char)(c0 + 2));
                vga_print_uint((unsigned int)(shown + 1), C_CYAN_ON_BLACK);
                vga_print(" ", C_GREY_ON_BLACK);
                vga_print(ew_words[i], C_YELLOW_ON_BLACK);
                shown++;
            }
        }
        /* meaning + status */
        vga_set_cursor((unsigned char)(r0 + 10), (unsigned char)(c0 + 2));
        vga_print(meaning[0] ? meaning : "type, tab+number completes",
                  C_GREY_ON_BLACK);
        vga_set_cursor((unsigned char)(r0 + 11), (unsigned char)(c0 + 2));
        vga_print(tab_armed ? "number?" : "enter first  esc close",
                  C_GREY_ON_BLACK);
        /* caret */
        vga_set_cursor((unsigned char)(r0 + 1),
                       (unsigned char)(c0 + 8 + ilen));
        mouse_draw();
        win_drag_update();
        k = div_getkey(&sh);
        mouse_hide();
        if (k == K_ESC) {
            win_close(id);
            mouse_hide();
            vga_clear();
            return;
        } else if (k == '\b') {
            if (ilen > 0) {
                ilen--;
                input[ilen] = 0;
            }
            tab_armed = 0;
        } else if (k == '\t' || k == '\n' || (k >= '1' && k <= '9')) {
            /* complete: tab arms, digit picks, enter takes first */
            int pick = -1;
            int n = 0;
            if (k == '\t') {
                tab_armed = 1;
                continue;
            }
            if (k >= '1' && k <= '9') {
                if (!tab_armed) continue;
                pick = k - '1';
                tab_armed = 0;
            }
            for (i = 0; i < EW_MAXW; i++) {
                if (ilen == 0 || ew_starts(ew_words[i], input)) {
                    if (pick < 0 || n == pick) {
                        unsigned int j = 0;
                        while (j < EW_MAXLEN && ew_words[i][j]) {
                            input[j] = ew_words[i][j];
                            j++;
                        }
                        input[j] = 0;
                        ilen = j;
                        {
                            unsigned int m = 0;
                            meaning[0] = 0;
                            while (m < 47 && ew_means[i][m]) {
                                meaning[m] = ew_means[i][m];
                                m++;
                            }
                            meaning[m] = 0;
                        }
                        break;
                    }
                    n++;
                }
            }
        } else if ((k >= 32 && k <= 126) || (unsigned char)k >= 128) {
            if (ilen < EW_MAXLEN) {
                input[ilen++] = k;
                input[ilen] = 0;
            }
            tab_armed = 0;
        }
        /* arrows/keys otherwise ignored; loop redraws */
    }
}
