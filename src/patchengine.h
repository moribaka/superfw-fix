/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
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

#ifndef _PATCHENGINE_H_
#define _PATCHENGINE_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "waitscan.h"
#include "irqpatch.h"

#define MAX_PATCH_OPS           128   // (artifically limited to save memory)
#define MAX_PATCH_PRG             4   // Only 4 programs can be encoded so far
#define PATCH_NATIVE_FLASH_READ_OPCODE 6

typedef struct {
  uint32_t length;
  uint8_t data[60];
} t_patch_prog;

struct struct_t_patch {
  uint8_t wcnt_ops;               // WaitCNT patches
  uint8_t save_ops;               // Save patches
  uint8_t save_mode;              // Save mode type (memory type)
  uint8_t irqh_ops;               // IRQ handler patches
  uint8_t rtc_ops;                // RTC patches
  uint8_t irq_flags;
  uint8_t web_edits;
  uint16_t image_kib;
  uint32_t menu_layout;            // Reviewed scatter layout, zero for legacy.
  uint32_t hole_size;             // Hole/trailing info, for ROM free space
  uint32_t hole_addr;
  uint32_t op[MAX_PATCH_OPS];     // Contain patch info for waitcnt and save
  t_patch_prog prgs[MAX_PATCH_PRG];
};

typedef struct struct_t_patch t_patch;

typedef struct {
  uint32_t wrapper, target, bank;
} t_flash_sector_link;

typedef struct {
  unsigned filesize;
  unsigned processed_bytes;
  bool enhanced_waitcnt;
  bool failed;
  uint8_t reference_key[5];
  uint8_t eeprom_read_types;
  unsigned wait_tail_size;
  #ifdef __GBA__
  uint32_t *wait_tail;          // SDRAM arena, copied using word accesses.
  #else
  uint32_t wait_tail[WAITSCAN_CONTEXT_SIZE / 4];
  #endif
  unsigned irq_boot_size;
  bool irq_nesting_guard;
  #ifdef __GBA__
  uint32_t *irq_boot;
  #else
  uint32_t irq_boot[IRQPATCH_BOOT_SIZE / 4];
  #endif
  // Save type related info
  unsigned save_type_guess;
  unsigned sram_patch_functions;
  unsigned flash64cnt, flash128cnt;
  unsigned flash_sector_count;
  #ifdef __GBA__
  t_flash_sector_link *flash_sector_links;
  #else
  t_flash_sector_link flash_sector_links[MAX_PATCH_OPS];
  #endif
  // RTC stuff
  bool rtc_guess;
  // Trailing data
  uint32_t ldata, ldatacnt;
  // The actual patch data.
  t_patch p;
} t_patch_builder;


// These new paths currently have actual SDRAM mGBA evidence only.
static inline bool patch_has_sdram_only_ops(const t_patch *patch) {
  if (!patch) return false;
  if (patch->menu_layout>=701 && patch->menu_layout<=705) return true;
  for (unsigned i=0;i<patch->wcnt_ops;) {
    unsigned op=patch->op[i]>>28,arg=(patch->op[i]>>25)&7;
    if(op==5 && arg==2)return true;
    unsigned n=op==3?1+(arg+4)/4:op==4?arg+2:1;
    if(n>patch->wcnt_ops-i)return true;
    i+=n;
  }
  return false;
}

void patchmem_dbinfo(const uint8_t *dbptr, uint32_t *pcnt, char *version, char *date, char *creator);
// Lookup routines (builtin, on-disk, etc).
bool patchmem_lookup(const uint8_t *gamecode, const uint8_t *dbptr, t_patch *pdata);
// Actual patching magic
bool patch_rtc_supported(const t_patch *patch);
bool patch_directsave_supported(const t_patch *patch);
bool patch_igm_supported(const t_patch *patch, bool nor);
bool patch_apply_rom(uint8_t *buffer, unsigned bufsize, uint32_t baseaddr, bool patch_waitcnt, const t_patch *pdata,
                     bool patch_rtc, uint32_t igmenu_addr, uint32_t ds_addr, bool nor);
// Payload patching routine
void payload_apply_rom(uint8_t *buffer, unsigned bufsize, uint32_t baseaddr,
                       const uint8_t *payload, unsigned payload_size, uint32_t payload_offset);

void patchengine_init(t_patch_builder *patch, unsigned filesize);
void patchengine_finalize(t_patch_builder *patch);
unsigned patchengine_select_save_reference(const t_patch_builder *builder, const uint8_t *database);
// Generates a patch set from a given ROM.
bool patchengine_process_rom(const uint32_t *rom, unsigned romsize, t_patch_builder *patch, void(*progresscb)(unsigned));

// Tries to load patches from disk
bool load_cached_patches(const char *romfn, t_patch *patches);
bool load_rom_patches(const char *romfn, t_patch *patches);
// Saves the patches to disk
bool write_patches_cache(const char *romfn, const t_patch *patches);

int serialize_patch(const t_patch *patch, uint8_t *buffer);
bool unserialize_patch(const uint8_t *buffer, unsigned size, t_patch *patch);


#endif

