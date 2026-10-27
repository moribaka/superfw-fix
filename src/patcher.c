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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "gbahw.h"
#include "fatfs/ff.h"
#include "common.h"
#include "patches.h"
#include "patchengine.h"
#include "romcompat.h"
#include "rtc_custom.h"
#include "ingame.h"

#pragma GCC optimize ("Os")

typedef struct {
  uint32_t signature;    // "PTDB" in ASCII
  uint32_t dbversion;    // Format version
  uint32_t patchcnt;     // Patch count
  uint32_t idxcnt;       // Number of IDX blocks
  char date[8];          // Creating date (ASCII encoded)
  char version[8];       // DB version (ASCII encoded)
  char creator[32];      // DB author/creator (ASCII encoded)
} t_db_header;

typedef struct {
  uint8_t gcode[4];
  uint32_t offset;       // LSB is game version (8 bits), MSB is byte offset
} t_db_idx;

// Game code is 4 bytes long (ascii chars in theory) + 1 byte for versioning
static int gcodecmp(const uint8_t *g1, const uint8_t *g2) {
  for (unsigned i = 0; i < 5; i++) {
    if (g1[i] < g2[i])
      return -1;
    if (g1[i] > g2[i])
      return 1;
  }
  return 0;
}

void patchmem_dbinfo(const uint8_t *dbptr, uint32_t *pcnt, char *version, char *date, char *creator) {
  const t_db_header *dbh = (t_db_header*)dbptr;
  *pcnt = dbh->patchcnt;
  memcpy(date, dbh->date, sizeof(dbh->date));
  memcpy(version, dbh->version, sizeof(dbh->version));
  memcpy(creator, dbh->creator, sizeof(dbh->creator));
}

// Routines to lookup patches from the patch database in memory
bool patchmem_lookup(const uint8_t *gamecode, const uint8_t *dbptr, t_patch *pdata) {
  pdata->menu_layout = 0;
  pdata->web_edits = 0;
  pdata->image_kib = 0;
  const t_db_header *dbh = (t_db_header*)dbptr;
  if (dbh->signature != 0x31424450 ||      // PTDB signature mismatch
      dbh->dbversion != 0x00010000)        // Version check
    return false;

  // Skip header and program block.
  const t_db_idx *dbidx = (t_db_idx*)&dbptr[1024];
  // Skip the index block to address data entries.
  const uint32_t *entries = (uint32_t*)&dbptr[1024 + 512 * dbh->idxcnt];

  // Load programs as well
  int pgn = 0;
  const uint8_t *pgrpage = &dbptr[512];
  for (int i = 0; i < MAX_PATCH_PRG; i++)
    pdata->prgs[i].length = 0;
  for (int i = 0; i < 512 && pgn < MAX_PATCH_PRG; i++) {
    unsigned cnt = pgrpage[i];
    if (!cnt)
      break;

    if (cnt > sizeof(pdata->prgs[pgn].data))
      return false;

    pdata->prgs[pgn].length = cnt;
    memcpy(pdata->prgs[pgn++].data, &pgrpage[i+1], cnt);
    i += cnt;
  }

  for (unsigned i = 0; i < dbh->patchcnt; i++) {
    if (!gcodecmp(dbidx[i].gcode, gamecode)) {
      uint32_t offset = dbidx[i].offset >> 8;
      const uint32_t *p = &entries[offset];
      const uint32_t pheader = *p++;

      pdata->wcnt_ops = (pheader >>  0) & 0xFF;
      pdata->save_ops = (pheader >>  8) & 0x1F;    // Only 5 bits
      pdata->irqh_ops = (pheader >> 16) & 0xFF;
      pdata->rtc_ops =  (pheader >> 24) & 0x0F;    // Only 4 bits
      pdata->irq_flags = 0;

      pdata->save_mode = (pheader >> 13) & 0x7;    // 3 bits

      const unsigned numops = pdata->wcnt_ops + pdata->save_ops + pdata->irqh_ops + pdata->rtc_ops;

      if ((pheader >> 28) & 0x1) {
        // Hole/Trailing space information, placed in the last op
        pdata->hole_addr = (p[numops] >> 16) << 10;   // In KiB chunks
        pdata->hole_size = (p[numops] & 0xFFFF) << 10;
      }

      // Copy patch words
      memcpy(&pdata->op[0], p, numops * sizeof(uint32_t));

      return true;
    }
  }

  return false;
}

// Write a byte to a buffer ensuring that only 16 bit accesses are performed.
static void write_mem8(uint8_t *mem, uint8_t bytedata) {
  uintptr_t ptraddr = (uintptr_t)mem;
  volatile uint16_t *aptr = (uint16_t*)(ptraddr & ~1U);
  unsigned sha = (ptraddr & 1) ? 8 : 0;
  uint16_t data = *aptr & (~(0xFF << sha));
  data |= (bytedata << sha);
  *aptr = data;
}

static void write_mem16(uint8_t *mem, uint16_t worddata) {
  write_mem8(mem + 0, worddata >>  0);
  write_mem8(mem + 1, worddata >>  8);
}

static void write_mem32(uint8_t *mem, uint32_t worddata) {
  write_mem8(mem + 0, worddata >>  0);
  write_mem8(mem + 1, worddata >>  8);
  write_mem8(mem + 2, worddata >> 16);
  write_mem8(mem + 3, worddata >> 24);
}

static void write_patch_word(uint8_t *buffer, unsigned size, uint32_t base,
                             uint32_t offset, uint32_t value) {
  for (unsigned byte = 0; byte < 4; byte++)
    if (offset + byte >= base && offset + byte - base < size)
      write_mem8(buffer + offset + byte - base, value >> (byte * 8));
}

static void copy_func16(uint8_t *buffer, unsigned bufsize, uint32_t baseaddr,
                        const uint16_t *fnptr, unsigned size, uint32_t offset) {
  uint32_t start = MAX(baseaddr, offset);
  uint32_t end = MIN(baseaddr + bufsize, offset + size);
  if (start >= end)
    return;
  volatile uint16_t *buf16 = (uint16_t*)(buffer + start - baseaddr);
  fnptr = (uint16_t*)(((uintptr_t)fnptr) & ~1U);           // Clear thumb addr bit for the symbol
  fnptr += (start - offset) / 2;
  for (unsigned i = start; i + 2 <= end; i += 2)
    *buf16++ = *fnptr++;
}

// Candidate extension: 0x54000000 | destination, source, byte count.
// Reversible overlapping moves repair unaligned resources without altering
// their contents or adjacent resources. Run for both SDRAM and NOR patches.
static bool apply_asset_move_ops(uint8_t *buffer, unsigned size, uint32_t base,
                                 const uint32_t *ops, unsigned count) {
  for (unsigned i=0;i<count;) {
    unsigned opcode=ops[i]>>28, arg=(ops[i]>>25)&7, words=1;
    if (opcode==3) words+=((arg+1)+3)/4;
    else if (opcode==4) words+=arg+1;
    else if (opcode==5 && arg==2) words=3;
    if (words>count-i) return false;
    if (opcode==5 && arg==2) {
      uint32_t dst=ops[i]&0x1ffffff, src=ops[i+1], n=ops[i+2];
      if (!n || (dst&3) || src>=0x2000000 || dst>=0x2000000 ||
          n>0x2000000-src || n>0x2000000-dst) return false;
      if (dst<base+size && base<dst+n) {
        // A partial ROM chunk may skip this whole move, but must never
        // perform a truncated move or read a source outside that chunk.
        if (src<base || dst<base || src+n>base+size || dst+n>base+size) return false;
        if (dst>src) {
          for (unsigned j=n;j;--j) write_mem8(buffer+dst-base+j-1,buffer[src-base+j-1]);
        } else {
          for (unsigned j=0;j<n;++j) write_mem8(buffer+dst-base+j,buffer[src-base+j]);
        }
      }
    }
    i+=words;
  }
  return true;
}

// Flashing/Eeprom routines flavours:
typedef struct {
  void *ptr;
  const uint32_t *size;
} f_func_info;

typedef struct {
  f_func_info eeprom_fncs[2];    // read and write
  f_func_info flash_fncs[6];
} t_psave_funcs;

typedef struct {
  uint32_t dspayload_addr;
  const t_psave_funcs *sfns;
} t_psave_info;

#define SFUNC(start, end) { (start), (end) - (start) },

static const t_psave_funcs psram_conversion_64k = {
  {
    { patch_eeprom_read_sram64k,        &patch_eeprom_read_sram64k_size },
    { patch_eeprom_write_sram64k,       &patch_eeprom_write_sram64k_size },
  },
  {
    { patch_flash_read_sram64k,         &patch_flash_read_sram64k_size },
    { patch_flash_erase_device_sram64k, &patch_flash_erase_device_sram64k_size },
    { patch_flash_erase_sector_sram64k, &patch_flash_erase_sector_sram64k_size },
    { patch_flash_write_sector_sram64k, &patch_flash_write_sector_sram64k_size },
    { patch_flash_write_byte_sram64k,   &patch_flash_write_byte_sram64k_size },
    { patch_flash_bank_sram,           &patch_flash_bank_sram_size },
  },
};

static const t_psave_funcs psram_conversion_128k = {
  {
    { patch_eeprom_read_sram64k,        &patch_eeprom_read_sram64k_size },
    { patch_eeprom_write_sram64k,       &patch_eeprom_write_sram64k_size },
  },
  {
    { patch_flash_read_sram128k,         &patch_flash_read_sram128k_size },
    { patch_flash_erase_device_sram128k, &patch_flash_erase_device_sram128k_size },
    { patch_flash_erase_sector_sram128k, &patch_flash_erase_sector_sram128k_size },
    { patch_flash_write_sector_sram128k, &patch_flash_write_sector_sram128k_size },
    { patch_flash_write_byte_sram128k,   &patch_flash_write_byte_sram128k_size },
    { patch_flash_bank_sram,            &patch_flash_bank_sram_size },
  },
};

static const t_psave_funcs pdirectsave = {
  {
    { patch_eeprom_read_directsave,        &patch_eeprom_read_directsave_size },
    { patch_eeprom_write_directsave,       &patch_eeprom_write_directsave_size },
  },
  {
    { patch_flash_read_directsave,         &patch_flash_read_directsave_size },
    { patch_flash_erase_device_directsave, &patch_flash_erase_device_directsave_size },
    { patch_flash_erase_sector_directsave, &patch_flash_erase_sector_directsave_size },
    { patch_flash_write_sector_directsave, &patch_flash_write_sector_directsave_size },
    { patch_flash_write_byte_directsave,   &patch_flash_write_byte_directsave_size },
  },
};

static const struct {
  const uint16_t *ptr;
  const uint32_t *size;
} rtc_fncs[] = {
  { patch_rtc_probe,       &patch_rtc_probe_size },
  { patch_rtc_reset,       &patch_rtc_reset_size },
  { patch_rtc_getstatus,   &patch_rtc_getstatus_size },
  { patch_rtc_gettimedate, &patch_rtc_gettimedate_size },
  { patch_rtc_settimedate, &patch_rtc_settimedate_size },
};


// Flash patching functions per-mode.

#define FN_THUMB_RET0     0x47702000
#define FN_THUMB_RET1     0x47702001
#define FN_ARM_RET0       0xe3a00000
#define FN_ARM_RET1       0xe3a00001
#define FN_ARM_RETBX      0xe12fff1e

void apply_patch_ops(
  // Where the ROM has been loaded.
  uint8_t *buffer, unsigned bufsize,
  // What base address this ROM has.
  uint32_t baseaddr,
  // Patch to apply
  const uint32_t *ops, unsigned pcount,
  const t_patch_prog *prgs, const t_psave_info *psi
) {
  for (unsigned i = 0; i < pcount; i++) {
    uint32_t opc = ops[i] >> 28;
    uint32_t arg = (ops[i] >> 25) & 7;
    uint32_t moff = ops[i] & 0x1FFFFFF;

    if ((opc == 8 || opc == 9) && (moff & 2)) {
      if (moff >= baseaddr && moff < baseaddr + bufsize)
        write_mem16(&buffer[moff - baseaddr], 0x46C0);
      moff += 2;
    }

    switch (opc) {
    case IRQPATCH_NESTING_OPCODE:
      i += 1;
      break;
    case PATCH_NATIVE_FLASH_READ_OPCODE:
      if (arg == IRQPATCH_VBLANK_ARGUMENT) i += 2;
      break;
    case IRQPATCH_DEAD_ARG_OPCODE:
      i += 6;
      break;
    case IRQPATCH_REGISTER_OPCODE:
      i += 4;
      break;
    case IRQPATCH_OPCODE:
    case IRQPATCH_DMA_OPCODE:
      i += 2;
      break;
    case IRQPATCH_STORE_OPCODE:
      i += arg != 0 ? 7 : 2;
      break;
    case 0x0:
      // Patch a full program into an address.
      for (unsigned j = 0; j < prgs[arg].length; j++)
        if (moff + j >= baseaddr && moff + j < baseaddr + bufsize)
          write_mem8(&buffer[moff + j - baseaddr], prgs[arg].data[j]);
      break;
    case 0x1:   // Patch Thumb instruction
      if (moff >= baseaddr && moff < baseaddr + bufsize)
        write_mem16(&buffer[moff - baseaddr], 0x46C0);   // mov r8, r8
      break;
    case 0x2:   // Patch ARM instruction
      if (moff >= baseaddr && moff < baseaddr + bufsize)
        write_mem32(&buffer[moff - baseaddr], 0xE1A00000);   // mov r0, r0
      break;
    case 0x3:   // Write N bytes to address
      for (unsigned index = 0; index < arg + 1; index++)
        if (moff + index >= baseaddr && moff + index < baseaddr + bufsize)
          write_mem8(&buffer[moff + index - baseaddr], ops[i + (index / 4) + 1] >> ((index & 3) * 8));
      i += (arg + 1 + 3) / 4;
      break;
    case 0x4:   // Write N words to address
      for (unsigned index = 0; index < (arg + 1) * 4; index++)
        if (moff + index >= baseaddr && moff + index < baseaddr + bufsize)
          write_mem8(&buffer[moff + index - baseaddr], ops[i + (index / 4) + 1] >> ((index & 3) * 8));
      i += arg + 1;
      break;
    case 0x5:   // Dummy function or pre-applied resource move
      if(arg==2){i+=2;break;}
      switch (arg) {
        case 0:
        case 1:
          write_patch_word(buffer, bufsize, baseaddr, moff, arg ? FN_THUMB_RET1 : FN_THUMB_RET0);
          break;
        case 4:
        case 5:
          write_patch_word(buffer, bufsize, baseaddr, moff, (arg == 5) ? FN_ARM_RET1 : FN_ARM_RET0);
          write_patch_word(buffer, bufsize, baseaddr, moff + 4, FN_ARM_RETBX);
          break;
      };
      break;

    case 0x7:    // RTC handlers
      if (arg < sizeof(rtc_fncs) / sizeof(rtc_fncs[0])) {
        unsigned fnsz = *rtc_fncs[arg].size;
        copy_func16(buffer, bufsize, baseaddr, rtc_fncs[arg].ptr, fnsz, moff);
        if (arg == 3 || arg == 4)
          write_patch_word(buffer, bufsize, baseaddr, moff + fnsz - 4,
                           GBA_ROM_BASE + moff - (arg == 3 ? 476 : 652));
      }
      break;

    case 0x8:    // EEPROM memory handlers
      if (arg < 2) {
        unsigned fnsz = *psi->sfns->eeprom_fncs[arg].size;
        copy_func16(buffer, bufsize, baseaddr, psi->sfns->eeprom_fncs[arg].ptr, fnsz, moff);
        if (psi->dspayload_addr)
          write_patch_word(buffer, bufsize, baseaddr, moff + fnsz, psi->dspayload_addr);
        break;
      }
      break;

    case 0x9:    // FLASH memory handlers
      if (arg < 6 && psi->sfns->flash_fncs[arg].ptr && psi->sfns->flash_fncs[arg].size) {
        unsigned fnsz = *psi->sfns->flash_fncs[arg].size;
        copy_func16(buffer, bufsize, baseaddr, psi->sfns->flash_fncs[arg].ptr, fnsz, moff);
        if (psi->dspayload_addr)
          write_patch_word(buffer, bufsize, baseaddr, moff + fnsz, psi->dspayload_addr);
      }
    };
  }
}


// Applies a patch directly into the ROM memory. The ROM can be a partial image
// (i.e. half a ROM or similar) but it should always be 4byte aligned (size too).
// Assuming we do at least 512 byte blocks or so too.
bool patch_apply_rom(
  // Where the ROM has been loaded.
  uint8_t *buffer, unsigned bufsize,
  // What base address this ROM has.
  uint32_t baseaddr,
  // Patching options
  bool patch_waitcnt,
  const t_patch *pdata,
  bool patch_rtc,
  uint32_t igmenu_addr,
  uint32_t ds_addr,
  bool nor
) {
  const uint32_t *ops = &pdata->op[0];
  if(nor && patch_has_sdram_only_ops(pdata))return false;
  if(!apply_asset_move_ops(buffer,bufsize,baseaddr,ops,pdata->wcnt_ops))return false;
  // Save patch routines vary depending on whether DirectSave is enabled or not.
  const t_psave_info psi = {
    .dspayload_addr = ds_addr,
    .sfns = ds_addr                                ? &pdirectsave :
            pdata->save_mode == SaveTypeFlash1024K ? &psram_conversion_128k
                                                   : &psram_conversion_64k
  };

  // Apply the WAIT CNT patches
  if (patch_waitcnt)
    apply_patch_ops(buffer, bufsize, baseaddr, ops, pdata->wcnt_ops, pdata->prgs, &psi);
  ops += pdata->wcnt_ops;

  // Apply save patches
  apply_patch_ops(buffer, bufsize, baseaddr, ops, pdata->save_ops, pdata->prgs, &psi);
  ops += pdata->save_ops;

  if (patch_waitcnt && !irqpatch_apply_nesting(buffer, bufsize, baseaddr, ops, pdata->irqh_ops))
    return false;
  // Apply optional patches, they are placed right after.
  if (igmenu_addr) {
    unsigned hooks;
    if (!irqpatch_hook_count(ops, pdata->irqh_ops, &hooks))
      return false;
    if (hooks) {
      uint32_t startup = ingame_menu_payload.startup_insts[0];
      if ((startup & 0xFFFFF000) != 0xE28F0000)
        return false;
      unsigned rotation = ((startup >> 8) & 15) * 2;
      uint32_t displacement = startup & 255;
      if (rotation)
        displacement = (displacement >> rotation) | (displacement << (32 - rotation));
      unsigned handler_offset = 8 + displacement;
      if (handler_offset >= ingame_menu_payload_size ||
          !irqpatch_apply(buffer, bufsize, baseaddr, ops, pdata->irqh_ops, igmenu_addr, handler_offset))
        return false;
    }
    if (!irqpatch_apply_vblank(buffer, bufsize, baseaddr, ops, pdata->irqh_ops,
          igmenu_addr + ingame_menu_payload.explicit_menu_entry,
          igmenu_addr + offsetof(t_igmenu, menu_hotkey))) return false;
    apply_patch_ops(buffer, bufsize, baseaddr, ops, pdata->irqh_ops, pdata->prgs, &psi);

    unsigned brop = 0xEA000000 | ((igmenu_addr - GBA_ROM_BASE - 8) >> 2);
    if (pdata->irq_flags & IRQPATCH_ENTRY_CHECKS_SCANNED)
      irqpatch_retarget_entry_checks_block(buffer, bufsize, baseaddr, ops, pdata->irqh_ops, brop);

    // Need to patch the header with some entrypoint detour.
    if (baseaddr == 0x0) {
      uint32_t ibranch = *(uint32_t*)buffer;
      uint32_t boot_addr = ((ibranch & 0xFFFFFF) << 2) + 8 + GBA_ROM_BASE;

      // Calculate the branch from 0x08000000 to igmenu_addr
      if (!(pdata->irq_flags & IRQPATCH_ENTRY_CHECKS_SCANNED))
        romcompat_apply_legacy_entry_checks(buffer, bufsize, 0, ibranch, brop);

      // Patch the first instruction with the branch opcode
      write_mem32(&buffer[0], brop);
      // Patch offset 0xB8 (unused header bits) with the real boot addr.
      write_mem32(&buffer[0xB8], boot_addr);
    }
  }
  ops += pdata->irqh_ops;

  // Apply RTC patches
  if (patch_rtc) {
    if (rtc_custom_supported(pdata))
      rtc_custom_apply(buffer, bufsize, baseaddr, pdata);
    else
      apply_patch_ops(buffer, bufsize, baseaddr, ops, pdata->rtc_ops, pdata->prgs, &psi);
  }

  return true;
}

// Applies a payload to the ROM memory.
void payload_apply_rom(
  // Where the ROM has been loaded.
  uint8_t *buffer, unsigned bufsize,
  // What base address this ROM has.
  uint32_t baseaddr,
  // Patch buffer to apply and its offset.
  const uint8_t *payload, unsigned payload_size,
  uint32_t payload_offset
) {
  // Optimize away the copy call if this can't possibly overlap.
  if (payload_offset > baseaddr + bufsize)
    return;
  if (baseaddr > payload_offset + payload_size)
    return;

  copy_func16(buffer, bufsize, baseaddr, (uint16_t*)payload, payload_size, payload_offset);
}

