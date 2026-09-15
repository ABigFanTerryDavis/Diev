/* Diev OS - RMFS: tiny filesystem living purely in RAM.
 * Copyright (C) 2026 Diev contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 *
 * No disk, no allocator: a fixed table of files in .bss (zeroed at boot).
 * Everything is lost on reboot. That is the point.
 */
#ifndef DIEV_RMFS_H
#define DIEV_RMFS_H

#define RMFS_MAX_FILES 16
#define RMFS_MAX_NAME  32
#define RMFS_MAX_SIZE  4096

/* 0 = ok, -1 = no such file / no free slot, -2 = too big, -3 = bad name */
void rmfs_init(void); /* must run once at boot: table lives in .bss */
int rmfs_write(const char *name, const char *data, unsigned int len);
int rmfs_read(const char *name, char *out, unsigned int out_max); /* returns size, or -1 */
int rmfs_rm(const char *name);
int rmfs_count(void);
void rmfs_stat(unsigned int *files, unsigned int *bytes);
/* Calls fn(name, size) for each file. Stops if fn returns nonzero. */
void rmfs_ls(void (*fn)(const char *name, unsigned int size));
/* Lists files under "dir", with names shown relative to it. */
void rmfs_ls_dir(const char *dir, void (*fn)(const char *name, unsigned int size));
void rmfs_debug_dump(void);

#endif
