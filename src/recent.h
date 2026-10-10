/*
 * Copyright (C) 2026 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __RECENT__H__
#define __RECENT__H__

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#define FLAG_RECENT_SD        0
#define FLAG_RECENT_NOR       1

typedef struct {
  uint16_t flags;            // Bit0: NOR (1) vs SD (0)
  uint16_t fname_offset;     // Basename offset in fpath (precalculated!)
  char fpath[MAX_FN_LEN];
} t_rentry;
_Static_assert (sizeof(t_rentry) % 4 == 0, "t_rentry must be word-friendly");

// Flush recent entries to disk
bool recent_flush(const t_rentry *rentries, unsigned rcount);

// Inserts a filename to the recently played games (or re-orders the list)
unsigned insert_recent_fn(t_rentry *rentries, unsigned rcount, const char *fn, unsigned flags);

// Deletes a recent entry
unsigned delete_recent(t_rentry *rentries, unsigned rcount, unsigned entry_num);

// Loads entries from disk
unsigned recent_load(const char *fpath, t_rentry *rentries);

#endif

