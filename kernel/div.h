/* Diev OS - div: RMFS file browser + viewer + editor.
 * Copyright (C) 2026 Diev contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE for details.
 *
 * The shell, the explorer and the editor are one thing: `div [dir]`
 * lists yellow files, arrows move, Enter opens, Shift+Enter edits.
 */
#ifndef DIEV_DIV_H
#define DIEV_DIV_H

void div_browse(const char *start_dir);
void div_view(const char *path);

#endif
