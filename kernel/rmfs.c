/* Diev OS - RMFS implementation.
 * Copyright (C) 2026 Diev contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 */
#include "rmfs.h"
#include "kernel.h"

typedef struct {
    char name[RMFS_MAX_NAME];
    unsigned int size;
    unsigned char data[RMFS_MAX_SIZE];
    int used;
} rmfs_file_t;

static rmfs_file_t rmfs_files[RMFS_MAX_FILES]; /* .bss: init via rmfs_init() */

void rmfs_init(void) {
    for (int i = 0; i < RMFS_MAX_FILES; i++) {
        rmfs_files[i].used = 0;
        rmfs_files[i].size = 0;
        rmfs_files[i].name[0] = 0;
    }
}

static unsigned int rstrlen(const char *s) {
    unsigned int n = 0;
    while (n < RMFS_MAX_NAME && s[n]) n++;
    return n;
}

static int rstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int valid_name(const char *name) {
    unsigned int n = rstrlen(name);
    if (n == 0 || n >= RMFS_MAX_NAME) return 0;
    if (name[0] == '/' || name[n - 1] == '/') return 0; /* folders: interior '/' only */
    for (unsigned int i = 0; i < n; i++) {
        char c = name[i];
        if (c == ' ') return 0;
        if (c == '/' && name[i + 1] == '/') return 0;
    }
    return 1;
}

static rmfs_file_t *find(const char *name) {
    for (int i = 0; i < RMFS_MAX_FILES; i++)
        if (rmfs_files[i].used && rstrcmp(rmfs_files[i].name, name) == 0)
            return &rmfs_files[i];
    return 0;
}

int rmfs_write(const char *name, const char *data, unsigned int len) {
    if (!valid_name(name)) return -3;
    if (len > RMFS_MAX_SIZE) return -2;
    rmfs_file_t *f = find(name);
    if (!f) {
        for (int i = 0; i < RMFS_MAX_FILES; i++) {
            if (!rmfs_files[i].used) {
                f = &rmfs_files[i];
                f->used = 1;
                unsigned int n = rstrlen(name);
                for (unsigned int k = 0; k <= n; k++)
                    f->name[k] = name[k];
                break;
            }
        }
        if (!f) return -1; /* table full */
    }
    for (unsigned int i = 0; i < len; i++)
        f->data[i] = (unsigned char)data[i];
    f->size = len;
    return 0;
}

int rmfs_read(const char *name, char *out, unsigned int out_max) {
    rmfs_file_t *f = find(name);
    if (!f) return -1;
    unsigned int n = f->size;
    if (n > out_max) n = out_max;
    for (unsigned int i = 0; i < n; i++)
        out[i] = (char)f->data[i];
    return (int)f->size;
}

int rmfs_rm(const char *name) {
    rmfs_file_t *f = find(name);
    if (!f) return -1;
    f->used = 0;
    f->size = 0;
    f->name[0] = 0;
    return 0;
}

int rmfs_count(void) {
    int n = 0;
    for (int i = 0; i < RMFS_MAX_FILES; i++)
        if (rmfs_files[i].used) n++;
    return n;
}

void rmfs_stat(unsigned int *files, unsigned int *bytes) {
    unsigned int f = 0, b = 0;
    for (int i = 0; i < RMFS_MAX_FILES; i++) {
        if (rmfs_files[i].used) {
            f++;
            b += rmfs_files[i].size;
        }
    }
    if (files) *files = f;
    if (bytes) *bytes = b;
}

void rmfs_ls(void (*fn)(const char *name, unsigned int size)) {
    for (int i = 0; i < RMFS_MAX_FILES; i++)
        if (rmfs_files[i].used)
            if (fn) fn(rmfs_files[i].name, rmfs_files[i].size);
}

/* List files under "dir" (shows names relative to it). */
static char ls_prefix[RMFS_MAX_NAME + 2];
static void (*ls_cb)(const char *, unsigned int);
static unsigned int ls_plen;

static void ls_dir_one(const char *name, unsigned int size) {
    unsigned int i = 0;
    while (i < ls_plen && ls_prefix[i] == name[i]) i++;
    if (i == ls_plen) ls_cb(name + ls_plen, size);
}

void rmfs_ls_dir(const char *dir, void (*fn)(const char *name, unsigned int size)) {
    unsigned int n = 0;
    while (n < RMFS_MAX_NAME && dir[n]) { ls_prefix[n] = dir[n]; n++; }
    while (n > 0 && ls_prefix[n - 1] == '/') n--; /* strip trailing slashes */
    if (n + 1 >= sizeof(ls_prefix)) return;
    ls_prefix[n++] = '/';
    ls_prefix[n] = 0;
    ls_plen = n;
    ls_cb = fn;
    rmfs_ls(ls_dir_one);
}

void rmfs_debug_dump(void) {
    vga_print("idx  used name                           size\n", C_GREY_ON_BLACK);
    for (int i = 0; i < RMFS_MAX_FILES; i++) {
        if (rmfs_files[i].used) {
            char idx[4];
            idx[0] = (char)('0' + i / 10);
            idx[1] = (char)('0' + i % 10);
            idx[2] = ' ';
            idx[3] = 0;
            vga_print(idx, C_GREY_ON_BLACK);
            vga_print(rmfs_files[i].name, C_YELLOW_ON_BLACK);
            {
                char sz[12];
                unsigned int n = rmfs_files[i].size;
                int j = 10;
                sz[10] = 0;
                if (n == 0) { sz[9] = '0'; j = 9; }
                while (n > 0 && j > 0) { sz[--j] = (char)('0' + (n % 10)); n /= 10; }
                vga_print(&sz[j], C_GREY_ON_BLACK);
            }
            vga_putchar('\n', C_GREY_ON_BLACK);
        }
    }
}
