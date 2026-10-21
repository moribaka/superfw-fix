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

#ifndef _CONFIG_H__
#define _CONFIG_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Config config maximum values here and there
#define MAX_FN_LEN                 256
#define FLASHG_MAXFN_CNT           32            // No more than 32 games in NOR
#define BROWSER_MAXFN_CNT     (16*1024)
#define RECENT_MAXFN_CNT          (200)

#define MAX_BACKUP_CNT             16

#define SUPERFW_DIR               "/.superfw"
#define ROMCONFIG_PATH            "/.superfw/config/"
#define PATCHDB_PATH              "/.superfw/patches/"
#define CHEATS_PATH               "/.superfw/cheats/"
#define EMULATORS_PATH            "/.superfw/emulators/"
#define GBC_EMULATOR_PATH         "/.superfw/emulators/gbc-emu.gba"
#define SETTINGS_FILEPATH         "/.superfw/settings.txt"
#define RECENT_FILEPATH           "/.superfw/recent.txt"
#define UISETTINGS_FILEPATH       "/.superfw/ui-settings.txt"
#define FLASHBACKUPTMP_FILEPATH   "/.superfw/flash_backup.tmp"
#define FLASHBACKUP_FILEPTRN      "/.superfw/flash_backup-%02x%02x%02x%02x.bin"

#define PENDING_SAVE_FILEPATH     "/.superfw/pending-save.txt"
#define PENDING_SRAM_TEST         "/.superfw/pending-sram-test.txt"

#endif

