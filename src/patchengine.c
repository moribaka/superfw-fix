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

// Patch engine
// This function tries to generate a patchset for a given ROM, finding:
//  - WAITCNT patch locations (for slow ROM/SDRAM)
//  - Save patches: Identify save type and patch SDK functions.
//  - IRQ handlers: Find IRQ installs and hook and redirect them.

// Heavily inspired on the patch tool at:
// https://github.com/davidgfnet/gba-patch-gen/blob/master/tools/save-finder.py
//
// Attempts to be as fast as possible by running on IWRAM and matching at
// word level, it still probably takes ~10-20 cycles per ROM byte.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifndef __GBA__
#include <stdio.h>
#include <stdlib.h>
#endif

#include "compiler.h"
#include "common.h"
#include "util.h"
#include "fatfs/ff.h"
#include "patchengine.h"
#include "romcompat.h"
#include "rtc_custom.h"
#ifdef __GBA__
#include "supercard_driver.h"
_Static_assert(WAITSCAN_CONTEXT_SIZE <= 8192, "Scan tail arena capacity");
_Static_assert(IRQPATCH_BOOT_SIZE <= 1024, "Scan boot arena capacity");
_Static_assert(sizeof(t_flash_sector_link) * MAX_PATCH_OPS <= 4096, "Scan link arena capacity");
_Static_assert(ROM_SCAN_LINKS_ADDR + 4096 <= GBA_ROM_BASE + ROM_OFF_USRPATCH_DB, "Scan arena overlaps assets");
#endif

#pragma GCC optimize ("Oz")

// Each aligned word is fetched once by the shared discovery loop. Only
// candidates enter the existing full validators; no ROM regions are omitted.
ARM_CODE IWRAM_CODE NOINLINE
static void scan_index_word(
    t_scan_index *index, unsigned offset, uint32_t word) {
  if (!word || word == UINT32_MAX) return;
  unsigned lo = word & 0xFFFF, hi = word >> 16, kinds = 0;
  if (lo == 0xB500 || hi == 0xB500) kinds |= SCAN_DMA;
  if (lo == 0xBC10 || (lo & 0xF800) == 0x4800 || lo == 0x6005 ||
      lo == 0x6008 || hi == 0x6008 || hi == 0xE3A0 || hi == 0xE59F || hi == 0xE51F)
    kinds |= SCAN_IRQ;
  if (word == 0xE3A03301) kinds |= SCAN_NEST;
  if (word == 0xE92D0007) kinds |= SCAN_ENTRY;
  if ((word >= 0x04000000 && word < 0x04000400) || hi == 0xE3A0 ||
      lo == 0xB590 || hi == 0xB590 || (lo & 0xF800) == 0x2000 || (hi & 0xF800) == 0x2000)
    kinds |= SCAN_WAIT;
  if ((lo & 0xF800) == 0x4800 || (hi & 0xF800) == 0x4800 || hi == 0xE59F || hi == 0xE51F)
    kinds |= SCAN_REFERENCE;
  if (kinds) {
    index->items[index->count].offset = offset;
    index->items[index->count++].kinds = kinds;
  }
}

// Explicit per-chunk discovery state; never allocated on the ARM call stack.
#ifdef __GBA__
#define SCAN_CANDIDATE_CAPACITY ((ROM_SCAN_BUFFER_SIZE + WAITSCAN_CONTEXT_SIZE) / 4)
#define SCAN_CANDIDATES_ADDR (ROM_SCAN_LINKS_ADDR + 4096)
#define SCAN_REFERENCES_ADDR (SCAN_CANDIDATES_ADDR + SCAN_CANDIDATE_CAPACITY * sizeof(t_scan_candidate))
_Static_assert(SCAN_REFERENCES_ADDR + (3 * SCAN_CANDIDATE_CAPACITY) * sizeof(t_scan_reference) + 512 <=
               GBA_ROM_BASE + ROM_OFF_USRPATCH_DB, "Candidate arena overlaps live assets");
static t_scan_index scan_index __attribute__((section(".sbss")));
#else
static t_scan_index scan_index;
static unsigned scan_capacity;
#endif

static bool prepare_scan_index(unsigned size, const uint32_t *prefix, unsigned prefix_size) {
  scan_index.count = scan_index.reference_count = scan_index.pending_irq_count = 0;
#ifdef __GBA__
  if (size > ROM_SCAN_BUFFER_SIZE || prefix_size > WAITSCAN_CONTEXT_SIZE) return false;
  scan_index.items = (t_scan_candidate*)SCAN_CANDIDATES_ADDR;
  scan_index.references = (t_scan_reference*)SCAN_REFERENCES_ADDR;
  scan_index.pending_irq = (uint32_t*)(SCAN_REFERENCES_ADDR + (3 * SCAN_CANDIDATE_CAPACITY) * sizeof(t_scan_reference));
#else
  unsigned capacity = (size + prefix_size) / 4 + 1;
  if (capacity > scan_capacity) {
    t_scan_candidate *items = realloc(scan_index.items, capacity * sizeof(*items));
    if (!items) return false;
    scan_index.items = items;
    // At most two Thumb references plus one ARM reference per word.
    t_scan_reference *references = realloc(scan_index.references, 3 * capacity * sizeof(*references));
    if (!references) return false;
    scan_index.references = references;
    scan_capacity = capacity;
  }
  if (!scan_index.pending_irq) {
    scan_index.pending_irq = malloc(512);
    if (!scan_index.pending_irq) return false;
  }

#endif
  // Prefix and current data are separate contiguous runs. Their hot loop
  // loads aligned words directly; full validators retain boundary guards.
  for (unsigned offset = 0; offset < prefix_size; offset += 4)
    scan_index_word(&scan_index, offset, prefix[offset / 4]);
  return true;
}


static bool reserve_operation(t_patch_builder *builder) {
  const t_patch *patch = &builder->p;
  if (patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops < MAX_PATCH_OPS)
    return true;
  builder->failed = true;
  return false;
}

static bool push_wait_operation(void *context, uint32_t operation) {
  t_patch_builder *builder = context;
  t_patch *patch = &builder->p;
  if (operation & WAITSCAN_REJECT_OPERATION) {
    operation &= ~WAITSCAN_REJECT_OPERATION;
    for (unsigned index = 0; index < patch->wcnt_ops; index++) {
      if (patch->op[index] != operation)
        continue;
      unsigned count = patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops;
      memmove32(&patch->op[index], &patch->op[index + 1], (count - index - 1) * 4);
      patch->wcnt_ops--;
      return true;
    }
    return true;
  }
  for (unsigned index = 0; index < patch->wcnt_ops; index++)
    if (patch->op[index] == operation)
      return true;
  if (!reserve_operation(builder))
    return false;
  memmove32(&patch->op[patch->wcnt_ops + 1], &patch->op[patch->wcnt_ops],
            (patch->save_ops + patch->irqh_ops + patch->rtc_ops) * 4);
  patch->op[patch->wcnt_ops++] = operation;
  return true;
}

static bool push_irq_operations(void *context, const uint32_t *operations, unsigned count) {
  t_patch_builder *builder = context;
  t_patch *patch = &builder->p;
  unsigned total = patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops;
  if (operations[0] >> 28 >= IRQPATCH_OPCODE && operations[0] >> 28 <= IRQPATCH_DEAD_ARG_OPCODE) {
    unsigned hooks;
    if (!irqpatch_hook_count(patch->op + patch->wcnt_ops + patch->save_ops, patch->irqh_ops, &hooks) ||
        hooks > IRQPATCH_MAX_HOOKS - ((operations[0] >> 28 == IRQPATCH_STORE_OPCODE &&
                                     ((operations[0] >> 25) & 7) != 0) ? 2 : 1)) {
      patch->irq_flags |= IRQPATCH_UNSUPPORTED;
      uint32_t metadata = IRQPATCH_METADATA_OPCODE << 28 | 1U << 25 | (operations[0] & 0x1FFFFFF);
      return push_irq_operations(context, &metadata, 1);
    }
  }
  if (count > MAX_PATCH_OPS - total) {
    builder->failed = true;
    return false;
  }
  unsigned offset = patch->wcnt_ops + patch->save_ops + patch->irqh_ops;
  memmove32(patch->op + offset + count, patch->op + offset, patch->rtc_ops * 4);
  memcpy(patch->op + offset, operations, count * 4);
  patch->irqh_ops += count;
  return true;
}

bool patch_igm_supported(const t_patch *patch, bool nor) {
  if(nor && patch_has_sdram_only_ops(patch))return false;
  if (!patch || !patch->irqh_ops || (patch->irq_flags & IRQPATCH_UNSUPPORTED) ||
      (patch->irq_flags & ~(IRQPATCH_BOOT_CLEAR | IRQPATCH_UNSUPPORTED | IRQPATCH_BOOT_RESTORE | IRQPATCH_ENTRY_CHECKS_SCANNED)) ||
      patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops > MAX_PATCH_OPS)
    return false;
  unsigned hooks;
  if (!irqpatch_hook_count(patch->op + patch->wcnt_ops + patch->save_ops, patch->irqh_ops, &hooks))
    return false;
  unsigned program_mask = 0;
  for (unsigned index = 0; index < MAX_PATCH_PRG; index++) {
    uint32_t address;
    if (patch->prgs[index].length != 4)
      continue;
    memcpy(&address, patch->prgs[index].data, 4);
    if (address == 0x03007FF4)
      program_mask |= 1U << index;
  }
  if (nor) {
    unsigned position = patch->wcnt_ops + patch->save_ops;
    if (patch->irqh_ops == 3 && (patch->op[position] >> 25) ==
        ((IRQPATCH_METADATA_OPCODE << 3) | IRQPATCH_VBLANK_ARGUMENT)) return false;
  }
  return (!nor || !hooks || patch->menu_layout == 3) && irqpatch_native_program_present(patch->op + patch->wcnt_ops + patch->save_ops, patch->irqh_ops, program_mask) &&
         (!(patch->irq_flags & (IRQPATCH_BOOT_CLEAR | IRQPATCH_BOOT_RESTORE)) || hooks);
}

#define WAITCNT_VALUE_EXACT  0x04000204
#define IRQHADDR_VALUE       0x03007FFC

// RTC word signatures
#define RTC_V_WORD0          0x52494953    // "SIIR"
#define RTC_V_WORD1          0x565f4354    // "TC_V"

// Savetype signatures (strings)
#define SRAM_V_WORD0         0x4D415253    // "SRAM"
#define SRAM_V_WORD1         0x3131565F    // "_V11"
#define SRAM_F_WORD1         0x565f465f    // "_F_V"
#define EEPROM_V_WORD0       0x52504545    // "EEPR"
#define EEPROM_V_WORD1       0x565F4D4F    // "OM_V"
#define FLASH_V_WORD0        0x53414C46    // "FLAS"
#define FLASH_V_WORD1        0x31565F48    // "H_V1"
#define FLASH512_WORD1       0x32313548    // "H512"
#define FLASH1M_WORD1        0x5F4D3148    // "H1M_"

#define GUESS_SRAM           (1 << 0)
#define GUESS_EEPROM         (1 << 1)
#define GUESS_FLASH          (1 << 2)
#define GUESS_FLASH64        (1 << 3)
#define GUESS_FLASH128       (1 << 4)

// Savetype function signatures.
#include "save_signatures.h"

#define THUMB_LDR_BACKOFF    256      // 8bit imm (scaled by 4 really)
#define ARM_LDR_BACKOFF     1024      // 12bit imm (not scaled)

#define OPC_WR_BUF      0x0
#define OPC_NOP_THUMB   0x1
#define OPC_NOP_ARM     0x2
#define OPC_COPY_BYTE   0x3
#define OPC_COPY_WORD   0x4
#define OPC_PATCH_FN    0x5
#define OPC_RTC_HD      0x7
#define OPC_EEPROM_HD   0x8
#define OPC_FLASH_HD    0x9

#define FUNC_RET0_THUMB   0

#define EEPROM_RD_HNDLR   0
#define EEPROM_WR_HNDLR   1

#define FLASH_READ_HNDLR  0
#define FLASH_CLRC_HNDLR  1
#define FLASH_CLRS_HNDLR  2
#define FLASH_WRTS_HNDLR  3
#define FLASH_WRBT_HNDLR  4
#define FLASH_BANK_HNDLR  5
#define FLASH_IDEN_HNDLR  6   // Invalid patch, just a placeholder!
#define FLASH_VERF_HNDLR  7   // Invalid patch, just a placeholder!

#define RTC_PROBE_HNDLR   0
#define RTC_RESET_HNDLR   1
#define RTC_STSRD_HNDLR   2
#define RTC_GETTD_HNDLR   3
#define RTC_SETTD_HNDLR   4

// TODO Not use memmove32 (arm-thumb jumps)

ARM_CODE IWRAM_CODE NOINLINE
static bool match_sig_prefix(const uint32_t *p, unsigned available, const uint16_t *sig, unsigned sigsize) {
  if (available < sigsize)
    return false;
  const uint16_t *p16 = (uint16_t*)p;
  for (unsigned i = 0; i < sigsize / 2; i++) {
    if (sig[i] && p16[i] != sig[i])
      return false;
  }
  return true;
}

// Start and target expressed in half-word offsets
static bool find_thumb_ldrpc(const uint16_t *rom, unsigned start, unsigned target) {
  for (unsigned i = start; i < target; i++) {
    unsigned opc = rom[i] >> 11;
    if (opc == 0x09) {
      // LDR rX, [pc + imm8*4]
      unsigned imm8 = rom[i] & 0xFF;
      unsigned tgtaddr = (i & ~1) + imm8 * 2 + 2;
      // Check for target (addr + off == target)
      if (tgtaddr == target)
        return true;
    }
  }
  return false;
}

// Start and target expressed in word offsets
static bool find_arm_ldrpc(const uint32_t *rom, unsigned start, unsigned target) {
  for (unsigned i = start; i < target; i++) {
    unsigned opc = (rom[i] >> 20) & 0xFF;
    unsigned rn  = (rom[i] >> 16) & 0x0F;
    if (opc == 0x59 && rn == 15) {
      // LDR rX, [pc + imm]
      unsigned imm12 = rom[i] & 0xFFF;
      if ((imm12 & 3) == 0) {    // Aligned load from pool
        unsigned tgtaddr = i + (imm12 >> 2) + 2;
        // Check for target (addr + off == target)
        if (tgtaddr == target)
          return true;
      }
    }
  }
  return false;
}

ARM_CODE IWRAM_CODE NOINLINE
static void push_save_handler(t_patch_builder *builder, unsigned savetype, unsigned hndltype, uint32_t addr) {
  if (!reserve_operation(builder))
    return;
  t_patch *patch = &builder->p;
  memmove32(&patch->op[patch->wcnt_ops+patch->save_ops+1],
            &patch->op[patch->wcnt_ops+patch->save_ops], (patch->irqh_ops + patch->rtc_ops) * 4);
  patch->op[patch->wcnt_ops + patch->save_ops++] = addr | (savetype << 28) | (hndltype << 25);
}

static void push_rtc_handler(t_patch_builder *builder, unsigned hndltype, uint32_t addr) {
  if (!reserve_operation(builder))
    return;
  t_patch *patch = &builder->p;
  patch->op[patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops++] = addr | (OPC_RTC_HD << 28) | (hndltype << 25);
}

static inline bool isromaddr(uint32_t addr) {
  return (addr >> 24) == 8 || (addr >> 24) == 9;
}

static inline bool isromramaddr(uint32_t addr) {
  return (addr >> 24) == 8 || (addr >> 24) == 9 || (addr >> 24) == 2 || (addr >> 24) == 3;
}

static bool modern_flash_literal(const uint8_t *data, unsigned size, unsigned offset,
                                 unsigned reg, uint32_t expected) {
  if (offset + 2 > size)
    return false;
  uint16_t instruction;
  memcpy(&instruction, data + offset, sizeof(instruction));
  if ((instruction & 0xFF00) != (0x4800 | (reg << 8)))
    return false;
  unsigned target = ((offset + 4) & ~3U) + (instruction & 255) * 4;
  if (target > size || size - target < 4)
    return false;
  uint32_t value;
  memcpy(&value, data + target, sizeof(value));
  if (expected == 0x08000000)
    return isromaddr(value);
  return expected ? value == expected : (value >> 24 == 2 || value >> 24 == 3);
}

static unsigned save_operation_span(uint32_t operation) {
  unsigned opcode = operation >> 28;
  unsigned argument = (operation >> 25) & 7;
  if (opcode == OPC_COPY_WORD)
    return argument + 2;
  if (opcode == OPC_COPY_BYTE)
    return 1 + (argument + 4) / 4;
  return 1;
}

static void push_modern_bank_call(t_patch_builder *builder, unsigned address, unsigned bank_address,
                                  unsigned distance, unsigned shift, bool restore_sector) {
  uint16_t instructions[8] = {0xB40F, shift, 0, 0, 0xBC0F, 0x46C0, 0x46C0, 0x46C0};
  int displacement = (int)bank_address - (int)(address + 8);
  if (displacement < -0x400000 || displacement >= 0x400000)
    return;
  instructions[2] = 0xF000 | (((uint32_t)displacement >> 12) & 0x7FF);
  instructions[3] = 0xF800 | (((uint32_t)displacement >> 1) & 0x7FF);
  unsigned words = restore_sector ? 4 : 3;
  if (restore_sector)
    instructions[5] = 0x4641;
  unsigned branch_offset = restore_sector ? 12 : 10;
  instructions[branch_offset / 2] = 0xE000 | ((distance - branch_offset - 4) / 2);
  t_patch *patch = &builder->p;
  unsigned total = patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops;
  if (total + words + 1 > MAX_PATCH_OPS) {
    builder->failed = true;
    return;
  }
  unsigned position = patch->wcnt_ops + patch->save_ops;
  memmove32(patch->op + position + words + 1, patch->op + position,
            (patch->irqh_ops + patch->rtc_ops) * 4);
  patch->op[position] = (OPC_COPY_WORD << 28) | ((words - 1) << 25) | address;
  memcpy(patch->op + position + 1, instructions, words * 4);
  patch->save_ops += words + 1;
}

static NOINLINE void scan_legacy_flashcard(const uint8_t *data, unsigned size, unsigned address,
                                 t_patch_builder *builder) {
  static const uint16_t bank[] = {
    0xB510, 0xB4FF, 0x4C0C, 0x4B08, 0x490C, 0x8023, 0x4B07, 0x4A08,
    0x800B, 0x4B0B, 0x0400, 0x801A, 0x0B40, 0x3B04, 0x8018, 0x4B05,
    0x800B, 0x8023, 0xBCFF, 0xE00D, 0x594A, 0x0000, 0x4F4B, 0x0000,
    0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x09FE, 0x0004, 0x09FE,
    0x0024, 0x09FE, 0xBD10
  };
  static const uint16_t read[] = {
    0xB5F0, 0x1C0C, 0x1C1E, 0x0424, 0x0436, 0x1C17, 0x1424, 0x1436,
    0xB4FF, 0x0400, 0x0C05, 0x0524, 0x0D00, 0x0000, 0xF7FF, 0xFEB4,
    0x032D, 0x0D24, 0x23E0, 0x0436, 0x192D, 0x051B, 0x0C36, 0x18ED,
    0x2E00, 0xD006, 0x782B, 0x3E01, 0x703B, 0x3501, 0x3701, 0x2E00,
    0xD1F8, 0xBCFF, 0xBDF0
  };
  if (size < 0x278 + sizeof(read) || data[0] != 0x12 || data[1] != 0xE0 ||
      data[2] != 0 || data[3] != 0 ||
      memcmp(data + 40, bank, sizeof(bank)) ||
      memcmp(data + 0x278, read, sizeof(read)))
    return;
  push_save_handler(builder, OPC_FLASH_HD, FLASH_BANK_HNDLR, address);
}

static NOINLINE void scan_modern_flash(const uint8_t *data, unsigned size, unsigned address,
                               t_patch_builder *builder) {
  static const uint16_t bank[] = {
    0x22AA, 0x2155, 0x4B05, 0x701A, 0x4A05, 0x7011, 0x22B0, 0x701A,
    0x23E0, 0x0600, 0x0E00, 0x051B, 0x7018, 0x4770, 0x5555, 0x0E00, 0x2AAA, 0x0E00
  };
  static const uint16_t identify[] = {
    0xB530, 0xB091, 0x466B, 0x2201, 0x1D5C, 0, 0, 0x601C, 0, 0x1A5B,
    0x03DB, 0x404A, 0x0C1B, 0xD00B, 0xA911, 0x1A88, 0x3842, 0x0011,
    0x8809, 0x3B01, 0x3202, 0x5211, 0x0419, 0x0C0B, 0x2900, 0xD1F6,
    0x22AA, 0x2155, 0, 0x701A, 0, 0x7011, 0x2290, 0x701A
  };
  static const uint16_t read[] = {
    0xB5F0, 0x2603, 0x46CE, 0x4647, 0, 0x882C, 0x4334, 0xB580,
    0x802C, 0x2580, 0, 0x4689, 0x6821, 0x4688, 0x6809, 0x0400,
    0x4694, 0xB0A0, 0x001A, 0x02AD, 0x0C03, 0x42A9, 0xD110, 0x2755,
    0, 0x36A7, 0x702E, 0, 0x7037, 0x26B0, 0x702E, 0x25E0,
    0x0100, 0x0E00, 0x052D, 0x7028, 0x6821, 0x200F, 0x4688, 0x4003
  };
  static const uint16_t verify[] = {
    0xB5F0, 0x2403, 0x46C6, 0x0403, 0, 0x8802, 0x4322, 0x2480,
    0xB500, 0x8002, 0, 0x6810, 0x6805, 0x468C, 0xB0C0, 0x0C19,
    0x02A4, 0x42A5, 0xD10F, 0x24AA, 0x2555, 0, 0x7004, 0,
    0x7025, 0x24B0, 0x7004, 0x20E0, 0x011B, 0x0E1B, 0x0500, 0x7003,
    0x230F, 0x6810, 0x4019
  };
  static const uint16_t partial[] = {
    0x0003, 0xB5F0, 0x46C6, 0x2580, 0, 0x041B, 0x0008, 0xB500,
    0x0C19, 0x4688, 0x6821, 0x468C, 0x6809, 0xB0C0, 0x02AD,
    0x42A9, 0xD112, 0x26AA, 0x2755, 0, 0x702E, 0, 0x7037,
    0x26B0, 0x702E, 0x25E0, 0x011B, 0x0E1B, 0x052D, 0x4641,
    0x702B, 0x230F, 0x4019, 0x6823, 0x4688, 0x469C
  };
  static const uint16_t read_second[] = {
    0xB5F0, 0x2503, 0x46C6, 0, 0x4694, 0x001A, 0x8823, 0x432B,
    0x2580, 0xB500, 0x8023, 0, 0x6823, 0x681E, 0x0400, 0x4688,
    0xB0A0, 0x0C07, 0x02AD, 0x42AE, 0xD10F, 0x25AA, 0x2655,
    0, 0x701D, 0, 0x702E, 0x25B0, 0x701D, 0x23E0, 0x0100,
    0x051B, 0x0E00, 0x7018, 0x230F, 0x401F, 0x6823
  };
  static const uint16_t verify_second[] = {
    0xB5F0, 0x2503, 0, 0x8822, 0x0003, 0x432A, 0x8022, 0x2480,
    0, 0x6816, 0x6835, 0x041B, 0x0008, 0xB0C0, 0x0C19,
    0x02A4, 0x42A5, 0xD10F, 0x25AA, 0x2655, 0, 0x7025,
    0, 0x702E, 0x25B0, 0x7025, 0x24E0, 0x011B, 0x0E1B,
    0x0524, 0x7023, 0x230F, 0x6816, 0x4019
  };
  static const uint16_t read_third[] = {
    0xB5F0,0x2503,0x46C6,0,0x4694,0x001A,0x8823,0x432B,
    0x2580,0xB500,0xB0A0,0x8023,0,0x6823,0x681E,0x0400,0x4688,
    0x0C07,0x02AD,0x42AE,0xD10F,0x25AA,0x2655,0,0x701D,0,
    0x702E,0x25B0,0x701D,0x23E0,0x0100,0x051B,0x0E00,0x7018,
    0x230F,0x401F,0x6823
  };
  static const uint16_t verify_third[] = {
    0xB5F0,0x2503,0,0x8822,0x0003,0x432A,0xB0C0,0x8022,
    0x2480,0,0x6816,0x6835,0x041B,0x0008,0x02A4,0x0C19,
    0x42A5,0xD10F,0x25AA,0x2655,0,0x7025,0,0x702E,0x25B0,
    0x7025,0x24E0,0x011B,0x0E1B,0x0524,0x7023,0x230F,0x6816,0x4019
  };
  static const uint16_t partial_third[] = {
    0xB5F0,0,0x6825,0x46AC,0x4666,0x0003,0x2580,0x6836,
    0x041B,0x0008,0xB0C0,0x0C19,0x02AD,0x42AE,0xD110,
    0x26AA,0x2755,0,0x702E,0,0x7037,0x26B0,0x702E,0x25E0,
    0x011B,0x0E1B,0x052D,0x702B,0x230F,0x4019,0x6823,0x469C
  };
  static const uint16_t partial_second[] = {
    0xB5F0, 0x2580, 0, 0x468C, 0x6821, 0x680E, 0x0403, 0xB0C0,
    0x0C1F, 0x02AD, 0x42AE, 0xD10F, 0x25AA, 0x2655, 0, 0x700D,
    0, 0x702E, 0x25B0, 0x700D, 0x21E0, 0x011B, 0x0E1B,
    0x0509, 0x700B, 0x230F, 0x6821, 0x401F
  };
  static const uint16_t bank_frame[] = {
    0xB580, 0xB081, 0xAF00, 0x0002, 0x003B, 0x701A, 0x4B09,
    0x22AA, 0x701A, 0x4B09, 0x2255, 0x701A, 0x4B06, 0x22B0,
    0x701A, 0x23E0, 0x051B, 0x003A, 0x7812, 0x701A, 0x46C0,
    0x46BD, 0xB001, 0xBC80, 0xBC01, 0x4700, 0x5555, 0x0E00, 0x2AAA, 0x0E00
  };
  static const uint16_t identify_frame[] = {
    0xB580, 0xB093, 0xAF00, 0x1D3B, 0x0018, 0, 0, 0x1D3B,
    0x3301, 0x64BB, 0, 0x22AA, 0x701A, 0, 0x2255, 0x701A,
    0, 0x2290, 0x701A
  };
  if ((address & 3) || address < 192)
    return;
  if (size >= sizeof(bank) && !memcmp(data, bank, sizeof(bank)))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_BANK_HNDLR, address);
  if (size >= sizeof(bank_frame) && !memcmp(data, bank_frame, sizeof(bank_frame)))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_BANK_HNDLR, address);
  if (match_sig_prefix((const uint32_t*)data, size, identify, sizeof(identify)) &&
      modern_flash_literal(data, size, 10, 3, 0) &&
      modern_flash_literal(data, size, 12, 1, 0x08000000) &&
      modern_flash_literal(data, size, 16, 3, 0x08000000) &&
      modern_flash_literal(data, size, 56, 3, 0x0E005555) &&
      modern_flash_literal(data, size, 60, 2, 0x0E002AAA))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_IDEN_HNDLR, address);
  if (match_sig_prefix((const uint32_t*)data, size, identify_frame, sizeof(identify_frame)) &&
      (*(const uint16_t*)(data + 10) & 0xF800) == 0xF000 &&
      (*(const uint16_t*)(data + 12) & 0xF800) == 0xF800 &&
      modern_flash_literal(data, size, 20, 3, 0x0E005555) &&
      modern_flash_literal(data, size, 26, 3, 0x0E002AAA) &&
      modern_flash_literal(data, size, 32, 3, 0x0E005555))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_IDEN_HNDLR, address);
  if (match_sig_prefix((const uint32_t*)data, size, read, sizeof(read)) &&
      modern_flash_literal(data, size, 8, 5, 0x04000204) &&
      modern_flash_literal(data, size, 20, 4, 0) &&
      modern_flash_literal(data, size, 48, 5, 0x0E005555) &&
      modern_flash_literal(data, size, 54, 6, 0x0E002AAA))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_READ_HNDLR, address);
  if (match_sig_prefix((const uint32_t*)data, size, read_second, sizeof(read_second)) &&
      modern_flash_literal(data, size, 6, 4, 0x04000204) &&
      modern_flash_literal(data, size, 22, 4, 0) &&
      modern_flash_literal(data, size, 46, 3, 0x0E005555) &&
      modern_flash_literal(data, size, 50, 5, 0x0E002AAA))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_READ_HNDLR, address);
  if (match_sig_prefix((const uint32_t*)data, size, read_third, sizeof(read_third)) &&
      modern_flash_literal(data, size, 6, 4, 0x04000204) &&
      modern_flash_literal(data, size, 24, 4, 0) &&
      modern_flash_literal(data, size, 46, 3, 0x0E005555) &&
      modern_flash_literal(data, size, 50, 5, 0x0E002AAA))
    push_save_handler(builder, OPC_FLASH_HD, FLASH_READ_HNDLR, address);
  uint32_t first;
  memcpy(&first, data, sizeof(first));
  if (first != 0x2403B5F0 && first != 0xB5F00003 && first != 0x2503B5F0 && first != 0x2580B5F0 &&
      (first & 0xF800FFFF) != 0x4800B5F0)
    return;
  const t_patch *patch = &builder->p;
  unsigned bank_address = 0;
  for (unsigned index = 0; index < patch->save_ops; index += save_operation_span(patch->op[patch->wcnt_ops + index])) {
    uint32_t operation = patch->op[patch->wcnt_ops + index];
    if ((operation & 0xFE000000) == ((OPC_FLASH_HD << 28) | (FLASH_BANK_HNDLR << 25)))
      bank_address = operation & 0x1FFFFFF;
  }
  if (!bank_address)
    return;
  if (match_sig_prefix((const uint32_t*)data, size, verify, sizeof(verify)) &&
      modern_flash_literal(data, size, 8, 0, 0x04000204) &&
      modern_flash_literal(data, size, 20, 2, 0) &&
      modern_flash_literal(data, size, 42, 0, 0x0E005555) &&
      modern_flash_literal(data, size, 46, 4, 0x0E002AAA))
    push_modern_bank_call(builder, address + 38, bank_address, 26, 0x0D18, false);
  if (match_sig_prefix((const uint32_t*)data, size, partial, sizeof(partial)) &&
      modern_flash_literal(data, size, 8, 4, 0) &&
      modern_flash_literal(data, size, 38, 5, 0x0E005555) &&
      modern_flash_literal(data, size, 42, 6, 0x0E002AAA))
    push_modern_bank_call(builder, address + 34, bank_address, 28, 0x0D18, true);
  if (match_sig_prefix((const uint32_t*)data, size, verify_second, sizeof(verify_second)) &&
      modern_flash_literal(data, size, 4, 4, 0x04000204) &&
      modern_flash_literal(data, size, 16, 2, 0) &&
      modern_flash_literal(data, size, 40, 4, 0x0E005555) &&
      modern_flash_literal(data, size, 44, 5, 0x0E002AAA))
    push_modern_bank_call(builder, address + 36, bank_address, 26, 0x0D18, false);
  if (match_sig_prefix((const uint32_t*)data, size, partial_second, sizeof(partial_second)) &&
      modern_flash_literal(data, size, 4, 4, 0) &&
      modern_flash_literal(data, size, 28, 1, 0x0E005555) &&
      modern_flash_literal(data, size, 32, 5, 0x0E002AAA))
    push_modern_bank_call(builder, address + 24, bank_address, 26, 0x0D18, false);
  if (match_sig_prefix((const uint32_t*)data, size, verify_third, sizeof(verify_third)) &&
      modern_flash_literal(data, size, 4, 4, 0x04000204) &&
      modern_flash_literal(data, size, 18, 2, 0) &&
      modern_flash_literal(data, size, 40, 4, 0x0E005555) &&
      modern_flash_literal(data, size, 44, 5, 0x0E002AAA))
    push_modern_bank_call(builder, address + 36, bank_address, 26, 0x0D18, false);
  if (match_sig_prefix((const uint32_t*)data, size, partial_third, sizeof(partial_third)) &&
      modern_flash_literal(data, size, 2, 4, 0) &&
      modern_flash_literal(data, size, 34, 5, 0x0E005555) &&
      modern_flash_literal(data, size, 38, 6, 0x0E002AAA))
    push_modern_bank_call(builder, address + 30, bank_address, 26, 0x0D18, false);
}

static unsigned recorded_sector_target(const t_patch_builder *builder, unsigned address) {
  const t_patch *patch = &builder->p;
  for (unsigned record = 0; record < builder->flash_sector_count; record++) {
    const t_flash_sector_link *link = builder->flash_sector_links + record;
    if (link->wrapper != address)
      continue;
    for (unsigned index = 0; index < patch->save_ops;
         index += save_operation_span(patch->op[patch->wcnt_ops + index]))
      if (patch->op[patch->wcnt_ops + index] ==
          ((OPC_FLASH_HD << 28) | (FLASH_BANK_HNDLR << 25) | link->bank))
        return link->target;
  }
  return address;
}

static NOINLINE unsigned flash_sector_patch_target(const uint32_t *rom, unsigned size, unsigned base,
                                          unsigned address, t_patch_builder *builder) {
  unsigned recorded = recorded_sector_target(builder, address);
  if (recorded != address)
    return recorded;
  static const uint16_t wrapper[] = {
    0xB500, 0, 0x681B, 0x895B, 0x0400, 0x0C00, 0x4283, 0xD802,
    0, 0xBC02, 0x4708, 0, 0, 0xE7FA
  };
  static const uint16_t implementation[] = {
    0xB5F0, 0x46D6, 0x464F, 0x4646, 0xB5C0, 0x0007, 0x0500, 0x0E00,
    0xB090, 0, 0, 0x200F, 0, 0x4699, 0, 0x4038, 0x4698, 0x23AA,
    0x4682, 0x2400, 0, 0x1C1E, 0x464A, 0x2003, 0x4657, 0,
    0x6812, 0x880B, 0x4383, 0x8A10, 0x4303, 0x800B
  };
  if (!builder->enhanced_waitcnt || address < base || address - base > size ||
      size - (address - base) < 36)
    return address;
  const uint8_t *data = (const uint8_t*)rom + address - base;
  unsigned remaining = size - (address - base);
  if (!match_sig_prefix((const uint32_t*)data, remaining, wrapper, sizeof(wrapper)) ||
      !modern_flash_literal(data, remaining, 2, 3, 0) ||
      !modern_flash_literal(data, remaining, 16, 0, 0x80FF))
    return address;
  uint16_t high, low;
  memcpy(&high, data + 22, 2);
  memcpy(&low, data + 24, 2);
  if ((high & 0xF800) != 0xF000 || (low & 0xF800) != 0xF800)
    return address;
  int displacement = ((high & 0x7FF) << 12) | ((low & 0x7FF) << 1);
  if (displacement & 0x400000)
    displacement -= 0x800000;
  int target = (int)address + 26 + displacement;
  if (target < (int)base || (unsigned)target - base > size || (target & 3) ||
      size - ((unsigned)target - base) < 84)
    return address;
  data = (const uint8_t*)rom + target - base;
  remaining = size - (target - base);
  if (!match_sig_prefix((const uint32_t*)data, remaining, implementation, sizeof(implementation)) ||
      !modern_flash_literal(data, remaining, 24, 3, 0) ||
      !modern_flash_literal(data, remaining, 28, 3, 0) ||
      !modern_flash_literal(data, remaining, 40, 5, 0x0E005555) ||
      !modern_flash_literal(data, remaining, 50, 1, 0x04000204))
    return address;
  memcpy(&high, data + 18, 2);
  memcpy(&low, data + 20, 2);
  if ((high & 0xF800) != 0xF000 || (low & 0xF800) != 0xF800)
    return address;
  displacement = ((high & 0x7FF) << 12) | ((low & 0x7FF) << 1);
  if (displacement & 0x400000)
    displacement -= 0x800000;
  unsigned bank = target + 22 + displacement;
  for (unsigned index = 0; index < builder->flash_sector_count; index++)
    if (builder->flash_sector_links[index].wrapper == address)
      return recorded_sector_target(builder, address);
  if (builder->flash_sector_count == MAX_PATCH_OPS) {
    builder->failed = true;
    return address;
  }
  t_flash_sector_link *link = builder->flash_sector_links + builder->flash_sector_count++;
  link->wrapper = address;
  link->target = target;
  link->bank = bank;
  return recorded_sector_target(builder, address);
}

static inline bool valid_flashid(uint16_t did) {
  static const uint16_t idtbl[] = {
    0x0000, 0x3D1F, 0xD4BF, 0x1B32,
    0x1CC2, 0x09C2, 0x1362 };
  for (unsigned i = 0; i < sizeof(idtbl) / sizeof(idtbl[0]); i++)
    if (idtbl[i] == did)
      return true;
  return false;
}

static inline bool isflash128k(uint16_t did) {
  return did == 0x09C2 || did == 0x1362;
}

// Flash info checks for structure recognition
#define SEEMS_FLASHINFO(st)  (                                       \
  (st)->zero_pad2 == 0 && (st)->zero_pad1 == 0 &&                    \
  ((st)->flash_size == 64*1024 || (st)->flash_size == 128*1024) &&   \
  ((st)->sector_size == 128 || (st)->sector_size == 4096) &&         \
  ((st)->shift_amount == 7 || (st)->shift_amount == 12) &&           \
  (st)->ws[0] < 4 && (st)->ws[1] < 4 &&                              \
  isromaddr((st)->program_sector_fnptr) &&                           \
  isromaddr((st)->erase_chip_fnptr) &&                               \
  isromaddr((st)->erase_sector_fnptr) &&                             \
  isromaddr((st)->wait_flash_write_fnptr) &&                         \
  isromramaddr((st)->timeout_lut_ptr))

#define FLASHINFO_VALIDSIZE(st) \
  ((st)->flash_size == (st)->sector_count * (st)->sector_size)

static void filter_save_ops(t_patch *p, unsigned optype) {
  // Filter save ops to match the specified type.
  for (unsigned i = 0; i < p->save_ops; ) {
    unsigned span = save_operation_span(p->op[p->wcnt_ops + i]);
    if ((p->op[p->wcnt_ops + i] >> 28) != optype) {
      if (optype == OPC_FLASH_HD && ((p->op[p->wcnt_ops + i] >> 28) == OPC_COPY_WORD ||
          (p->op[p->wcnt_ops + i] >> 28) == PATCH_NATIVE_FLASH_READ_OPCODE)) {
        i += span;
        continue;
      }
      memmove32(&p->op[p->wcnt_ops + i], &p->op[p->wcnt_ops + i + span],
                (p->save_ops - span - i + p->irqh_ops + p->rtc_ops) * 4);
      p->save_ops -= span;
    } else {
      i += span;
    }
  }
}

static NOINLINE void scan_native_flash_reader(const uint8_t *data, unsigned size,
                                             unsigned address, t_patch_builder *builder) {
  static const uint16_t function[] = {
    0xB570,0xB0A0,0x0300,0x1840,0x21E0,0x0509,0x1809,0x7808,0x7010,
    0x3B01,0x3201,0x3101,0x2B00,0xD1F8,0x2000,0xB020,0xBC70,0xBC02,0x4708
  };
  if ((address & 3) || address < 192 || size < sizeof(function) || memcmp(data, function, sizeof(function)))
    return;
  push_save_handler(builder, PATCH_NATIVE_FLASH_READ_OPCODE, 0, address);
}

static NOINLINE void scan_prepatched_eeprom(const uint8_t *data, unsigned size,
                                   unsigned address, t_patch_builder *builder) {
  static const uint16_t read_function[] = {
    0xB570, 0x0400, 0x1C0A, 0x0B40, 0x21E0, 0x0509, 0x1841,
    0x3107, 0x2300, 0x7808, 0x7010, 0x3301, 0x3201, 0x3901,
    0x2B07, 0xD9F8, 0x2000, 0xBC70, 0xBC02, 0x4708
  };
  static const uint16_t write_function[] = {
    0xB570, 0x0400, 0x1C0A, 0x0B40, 0x21E0, 0x0509, 0x1841,
    0x3107, 0x2300, 0x7810, 0x7008, 0x3301, 0x3201, 0x3901,
    0x2B07, 0xD9F8, 0x2000, 0xBC70, 0xBC02, 0x4708
  };
  static const uint16_t forward_read[] = {
    0xB570, 0x0400, 0x1C0A, 0x0B40, 0x21E0, 0x0509, 0x1841,
    0x2300, 0x7808, 0x7010, 0x3301, 0x3201, 0x3101,
    0x2B07, 0xD9F8, 0x2000, 0xBC70, 0xBC02, 0x4708
  };
  static const uint16_t forward_write[] = {
    0xB570, 0x0400, 0x1C0A, 0x0B40, 0x21E0, 0x0509, 0x1841,
    0x2300, 0x7810, 0x7008, 0x3301, 0x3201, 0x3101,
    0x2B07, 0xD9F8, 0x2000, 0xBC70, 0xBC02, 0x4708
  };
  if (address < 192 || size < sizeof(forward_read))
    return;
  if ((size >= sizeof(read_function) && !memcmp(data, read_function, sizeof(read_function))) ||
      !memcmp(data, forward_read, sizeof(forward_read)))
    builder->sram_patch_functions |= 1;
  if ((size >= sizeof(write_function) && !memcmp(data, write_function, sizeof(write_function))) ||
      !memcmp(data, forward_write, sizeof(forward_write)))
    builder->sram_patch_functions |= 2;
}

bool patch_directsave_supported(const t_patch *patch) {
  if (!patch || patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops > MAX_PATCH_OPS)
    return false;
  for (unsigned index = 0; index < patch->save_ops; index++) {
    uint32_t operation = patch->op[patch->wcnt_ops + index];
    if (operation >> 28 == PATCH_NATIVE_FLASH_READ_OPCODE)
      return false;
    if (operation >> 28 == OPC_FLASH_HD && ((operation >> 25) & 7) == FLASH_BANK_HNDLR)
      return false;
  }
  return true;
}

bool patch_rtc_supported(const t_patch *patch) {
  if (!patch || patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops > 128)
    return false;
  if (rtc_custom_supported(patch))
    return true;
  const uint32_t *operations = patch->op + patch->wcnt_ops + patch->save_ops + patch->irqh_ops;
  unsigned handlers = 0;
  for (unsigned index = 0; index < patch->rtc_ops; index++) {
    uint32_t operation = operations[index];
    unsigned handler = (operation >> 25) & 7;
    if (operation >> 28 != OPC_RTC_HD || handler > RTC_SETTD_HNDLR)
      return false;
    handlers |= 1U << handler;
    if (handler == RTC_GETTD_HNDLR || handler == RTC_SETTD_HNDLR) {
      unsigned distance = handler == RTC_GETTD_HNDLR ? 504 : 176;
      unsigned dependency = handler == RTC_GETTD_HNDLR ? RTC_RESET_HNDLR : RTC_GETTD_HNDLR;
      unsigned offset = operation & 0x1FFFFFF;
      bool found = false;
      for (unsigned other = 0; other < patch->rtc_ops; other++)
        if (offset >= distance && operations[other] == ((OPC_RTC_HD << 28) | (dependency << 25) | (offset - distance)))
          found = true;
      if (!found)
        return false;
    }
  }
  return (handlers & 15) == 15;
}

void patchengine_init(t_patch_builder *patchb, unsigned filesize) {
  memset(patchb, 0, sizeof(*patchb));
#ifdef __GBA__
  patchb->wait_tail = ROM_SCAN_TAIL_U32;
  patchb->irq_boot = ROM_SCAN_BOOT_U32;
  patchb->flash_sector_links = (t_flash_sector_link*)ROM_SCAN_LINKS_ADDR;
#endif

  patchb->filesize = filesize;

  // Since we mostly patch WAITCNT constants, generate a program that zeroes out a 32 bit word
  patchb->p.prgs[0].length = 4;
  // For IRQ writes, we overwrite with the redirected address.
  uint32_t handleraddr = 0x03007FF4;
  patchb->p.prgs[1].length = sizeof(handleraddr);
  memcpy(patchb->p.prgs[1].data, &handleraddr, sizeof(handleraddr));

  // Flash Ident Stub functions (64 and 128KB flavours)
  const uint16_t flash64_stub[] = {
    0x201c,  // movs r0, #0x1c     - (Macronix 64KB flash device)
    0x0200,  // lsls r0, r0, #8
    0x30c2,  // adds r0, #0xc2
    0x4770,  // bx lr
  };
  const uint16_t flash128_stub[] = {
    0x2009,  // movs r0, #0x09     - Macronix 128KB flash device
    0x0200,  // lsls r0, r0, #8
    0x30c2,  // adds r0, #0xc2
    0x4770,  // bx lr
  };
  patchb->p.prgs[2].length = sizeof(flash64_stub);
  memcpy(patchb->p.prgs[2].data, flash64_stub, sizeof(flash64_stub));
  patchb->p.prgs[3].length = sizeof(flash128_stub);
  memcpy(patchb->p.prgs[3].data, flash128_stub, sizeof(flash128_stub));
}

IWRAM_CODE NOINLINE unsigned patchengine_select_save_reference(const t_patch_builder *builder, const uint8_t *database) {
  unsigned guess = builder->save_type_guess;
  if (!builder->enhanced_waitcnt || !guess || !(guess & (guess - 1)) || !database ||
      *(const uint32_t*)database != 0x31424450 || *(const uint32_t*)(database + 4) != 0x00010000)
    return SaveTypeNone;
  unsigned count = *(const uint32_t*)(database + 8);
  const uint32_t *records = (const uint32_t*)(database + 1024 + 512 * *(const uint32_t*)(database + 12));
  for (unsigned entry = 0; entry < count; entry++) {
    const uint8_t *index = database + 1024 + entry * 8;
    if (memcmp(index, builder->reference_key, 5))
      continue;
    const uint32_t *record = records + (*(const uint32_t*)(index + 4) >> 8);
    unsigned header = *record++, mode = header >> 13 & 7;
    unsigned opcode = mode == SaveTypeEEPROM4K || mode == SaveTypeEEPROM64K ? 8 :
                      mode == SaveTypeFlash512K || mode == SaveTypeFlash1024K ? 9 : 0;
    if (!opcode || (opcode == 8 && builder->sram_patch_functions == 3))
      return SaveTypeNone;
    unsigned reference_count = header >> 8 & 31, found = 0;
    record += header & 255;
    for (unsigned position = 0; position < reference_count; ) {
      uint32_t expected = record[position];
      unsigned argument = expected >> 25 & 7;
      unsigned role = argument == 0 ? 1 : opcode == 8 && argument == 1 ? 2 :
                      opcode == 9 && (argument == 3 || argument == 4) ? 2 : 0;
      if (expected >> 28 == opcode && role) {
        for (unsigned current = 0; current < builder->p.save_ops; ) {
          uint32_t operation = builder->p.op[builder->p.wcnt_ops + current];
          if (operation == expected)
            found |= role;
          current += save_operation_span(operation);
        }
      }
      position += save_operation_span(expected);
    }
    return found == 3 ? mode : SaveTypeNone;
  }
  return SaveTypeNone;
}

static IWRAM_CODE NOINLINE unsigned select_save_reference(const t_patch_builder *builder) {
  if (!builder->enhanced_waitcnt || !(builder->save_type_guess & (builder->save_type_guess - 1)))
    return SaveTypeNone;
#ifdef __GBA__
  set_supercard_mode(MAPPED_SDRAM, true, false);
  unsigned mode = patchengine_select_save_reference(builder, (const uint8_t*)ROM_PATCHDB_U8);
  set_supercard_mode(MAPPED_SDRAM, true, true);
  return mode;
#else
  static uint8_t *database;
  if (!database) {
    char path[4096];
    const char *source = __FILE__, *separator = strrchr(source, '/');
    const char *windows_separator = strrchr(source, '\\');
    if (!separator || (windows_separator && windows_separator > separator))
      separator = windows_separator;
    if (!separator || (unsigned)(separator - source) + 20 >= sizeof(path))
      return SaveTypeNone;
    unsigned length = separator - source;
    memcpy(path, source, length);
    strcpy(path + length, "/../res/patches.db");
    FILE *file = fopen(path, "rb");
    if (!file)
      return SaveTypeNone;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size >= 1024) {
      database = malloc(size);
      if (database && fread(database, 1, size, file) != (unsigned long)size) {
        free(database);
        database = NULL;
      }
    }
    fclose(file);
  }
  return patchengine_select_save_reference(builder, database);
#endif
}

static void note_padding_run(t_patch_builder *builder, unsigned end) {
  if (!builder->enhanced_waitcnt || builder->filesize < MAX_ROM_SIZE_IGM ||
      (builder->ldata != 0 && builder->ldata != UINT32_MAX) || builder->ldatacnt < 128 * 1024)
    return;
  unsigned start = ROUND_UP2(end - builder->ldatacnt, 1024);
  unsigned size = (end & ~1023U) - start;
  if (size > builder->p.hole_size) {
    builder->p.hole_addr = start;
    builder->p.hole_size = size;
  }
}

void patchengine_finalize(t_patch_builder *patchb) {
  for (unsigned index = 0; index < patchb->p.save_ops;
       index += save_operation_span(patchb->p.op[patchb->p.wcnt_ops + index])) {
    uint32_t *operation = patchb->p.op + patchb->p.wcnt_ops + index;
    if ((*operation & 0xFE000000) == 0x94000000)
      *operation = 0x94000000 | recorded_sector_target(patchb, *operation & 0x1FFFFFF);
  }
  unsigned reference_mode = select_save_reference(patchb);
  if (reference_mode)
    patchb->save_type_guess = reference_mode == SaveTypeEEPROM4K || reference_mode == SaveTypeEEPROM64K ? GUESS_EEPROM :
                              reference_mode == SaveTypeFlash1024K ? GUESS_FLASH128 : GUESS_FLASH64;
  t_patch *p = &patchb->p;
  if (!patchb->save_type_guess && p->save_ops) {
    bool only_metadata = true;
    for (unsigned index = 0; index < p->save_ops; index++)
      if (p->op[p->wcnt_ops + index] >> 28 != PATCH_NATIVE_FLASH_READ_OPCODE)
        only_metadata = false;
    if (only_metadata)
      filter_save_ops(p, 0xF);
  }
  if (patchb->save_type_guess == 0 && p->save_ops == 0)
    p->save_mode = SaveTypeNone;         // No saving strings nor signatures found!
  else if (patchb->save_type_guess == GUESS_SRAM) {
    p->save_mode = SaveTypeSRAM;
    filter_save_ops(p, 0xF);             // Clear all save opcodes!
  }
  else if (patchb->save_type_guess == GUESS_EEPROM && patchb->enhanced_waitcnt &&
           patchb->sram_patch_functions == 3 && p->save_ops == 0) {
    p->save_mode = SaveTypeSRAM;
  }
  else if (patchb->save_type_guess == GUESS_EEPROM) {
    p->save_mode = reference_mode == SaveTypeEEPROM4K ||
                   (patchb->enhanced_waitcnt && patchb->eeprom_read_types == 1) ? SaveTypeEEPROM4K : SaveTypeEEPROM64K;
    filter_save_ops(p, OPC_EEPROM_HD);   // Clear other opcodes but EEPROM
  }
  else if (patchb->save_type_guess == GUESS_FLASH ||
           patchb->save_type_guess == GUESS_FLASH64 ||
           patchb->save_type_guess == GUESS_FLASH128) {
    // Guess flash size.
    p->save_mode = (patchb->save_type_guess == GUESS_FLASH128) ? SaveTypeFlash1024K
                                                               : SaveTypeFlash512K;
    filter_save_ops(p, OPC_FLASH_HD);    // Filter all opcodes but FLASH
  }
  else if (p->save_ops == 0 && (patchb->save_type_guess & GUESS_SRAM)) {
    // Could not find any signatures, but SRAM looks promising.
    p->save_mode = SaveTypeSRAM;
  }
  else {
    // Multiple types, or not a clue, fallback to SRAM
    p->save_mode = SaveTypeSRAM;
    filter_save_ops(p, 0xF);             // Clear all save opcodes!
  }

  // Process any FLASH_IDEN_HNDLR handlers into program patches.
  // These stub out the ident function to return some hardcoded device ID.
  for (unsigned i = 0; i < p->save_ops; i++) {
    unsigned span = save_operation_span(p->op[p->wcnt_ops + i]);
    if (span > 1) {
      i += span - 1;
      continue;
    }
    if ((p->op[p->wcnt_ops + i] >> 28) == OPC_FLASH_HD) {
      unsigned subop = ((p->op[p->wcnt_ops + i] >> 25) & 7);
      if (subop == FLASH_IDEN_HNDLR) {
        // Replace the ident function with one of the two possible stubs.
        unsigned num = (patchb->save_type_guess == GUESS_FLASH128) ? 3 : 2;
        p->op[p->wcnt_ops + i] = (p->op[p->wcnt_ops + i] & 0x1FFFFFF)
                                 | (OPC_WR_BUF << 28) | (num << 25);
      }
      else if (subop == FLASH_VERF_HNDLR) {
        // Patch function with return 0 (thumb)
        p->op[p->wcnt_ops + i] = (p->op[p->wcnt_ops + i] & 0x1FFFFFF)
                                 | (OPC_PATCH_FN << 28) | (FUNC_RET0_THUMB << 25);
      }
    }
  }

  // Clear all the ops at the end (if any)
  for (unsigned i = p->wcnt_ops + p->save_ops + p->irqh_ops + p->rtc_ops; i < MAX_PATCH_OPS; i++)
    p->op[i] = 0;

  // If the rom is big, annotate the trailing data.
  if (patchb->filesize >= MAX_ROM_SIZE_IGM && patchb->ldatacnt >= 4096) {
    // Calculate address and hole size, round up!
    uint32_t saddr = patchb->filesize - patchb->ldatacnt;
    uint32_t eaddr = patchb->filesize;

    uint32_t saddrr = ROUND_UP2(saddr, 1024);
    uint32_t eaddrr = eaddr & ~1023U;
    uint32_t size = eaddrr - saddrr;

    if (size > patchb->p.hole_size) {
      patchb->p.hole_addr = saddrr;
      patchb->p.hole_size = size;
    }
  }
}

// Generates a patch set from a given ROM.
// Walks a ROM (or chunk of a ROM) and looks for certain constants to calculate
// WAITCNT, IRQ and save patches. Generate a patch structure out of it.
// Cheap first-word dispatch for the save/RTC/legacy matcher. The full
// signatures and FLASH-table guards remain unchanged after this filter.
static inline __attribute__((always_inline)) bool main_scan_candidate(uint32_t word) {
  if (isromaddr(word) || (word >> 16) == 0xB570) return true;
  switch (word & 0xFFFF) {
    case 0x0003:
    case 0x0204:
    case 0x0600:
    case 0x22AA:
    case 0x4545:
    case 0x4953:
    case 0x4C46:
    case 0x5253:
    case 0x7FFC:
    case 0xB500:
    case 0xB530:
    case 0xB570:
    case 0xB580:
    case 0xB590:
    case 0xB5B0:
    case 0xB5F0:
    case 0xE012:
      return true;
    default: return false;
  }
}

static NOINLINE bool scan_enhanced_boot(const uint32_t *rom, unsigned romsize, t_patch_builder *patchb,
                                      void (*progress)(unsigned)) {
  t_patch *patch = &patchb->p;
  if (patchb->enhanced_waitcnt && patchb->processed_bytes < IRQPATCH_BOOT_SIZE) {
    unsigned count = MIN(romsize, IRQPATCH_BOOT_SIZE - patchb->processed_bytes);
#ifdef __GBA__
    memcpy32((uint8_t *)patchb->irq_boot + patchb->processed_bytes, rom, ROUND_UP2(count, 4));
#else
    memcpy((uint8_t *)patchb->irq_boot + patchb->processed_bytes, rom, count);
#endif
    patchb->irq_boot_size += count;
    patchb->irq_nesting_guard = irqpatch_nesting_family((const uint8_t *)patchb->irq_boot, patchb->irq_boot_size);
    if ((patchb->irq_boot_size == IRQPATCH_BOOT_SIZE ||
         patchb->processed_bytes + romsize == patchb->filesize) &&
        (!irqpatch_scan_boot((uint8_t *)patchb->irq_boot, patchb->irq_boot_size, &patch->irq_flags,
                             push_irq_operations, patchb) ||
         !irqpatch_scan_intro((uint8_t *)patchb->irq_boot, patchb->irq_boot_size, patchb->filesize,
                              &patch->irq_flags, push_irq_operations, patchb)))
      return false;
  }
  return true;
}

static NOINLINE bool scan_enhanced_candidates(const uint32_t *rom, unsigned romsize, t_patch_builder *patchb) {
  t_patch *patch = &patchb->p;
  if (patchb->enhanced_waitcnt &&
      !irqpatch_scan_dma_progress_indexed((const uint8_t *)rom, romsize, patchb->processed_bytes,
                         (const uint8_t *)patchb->wait_tail, patchb->wait_tail_size,
                         &patch->irq_flags, push_irq_operations, patchb, NULL, &scan_index))
    return false;
  if (patchb->enhanced_waitcnt &&
      !irqpatch_scan_register_progress_indexed((const uint8_t *)rom, romsize, patchb->processed_bytes,
                              (const uint8_t *)patchb->wait_tail, patchb->wait_tail_size,
                              patchb->filesize, &patch->irq_flags, push_irq_operations, patchb, NULL, &scan_index))
    return false;
  if (patchb->enhanced_waitcnt && patchb->irq_nesting_guard &&
      !irqpatch_scan_nesting_indexed((const uint8_t *)rom, romsize, patchb->processed_bytes,
                             (const uint8_t *)patchb->wait_tail, patchb->wait_tail_size,
                             push_irq_operations, patchb, &scan_index))
    return false;
  if (patchb->enhanced_waitcnt) {
    if (!romcompat_scan_entry_checks_indexed((const uint8_t*)rom, romsize, patchb->processed_bytes,
                                    (const uint8_t*)patchb->wait_tail, patchb->wait_tail_size,
                                    patchb->irq_boot[0], push_irq_operations, patchb, &scan_index))
      return false;
    patch->irq_flags |= IRQPATCH_ENTRY_CHECKS_SCANNED;
  }
  return true;
}

// Full validation is cold relative to the shared word loop. Keep this
// matcher out of the IWRAM loop; only filtered candidates pay the call.
static NOINLINE bool process_save_candidate(const uint32_t *rom, unsigned romsize,
    t_patch_builder *patchb, unsigned *position, uint32_t word) {
  unsigned i = *position;
  const uint16_t *rom16 = (const uint16_t*)rom;
    if (patchb->enhanced_waitcnt && (word & 0xF800FFFF) == 0x4800B500)
      flash_sector_patch_target(rom, romsize, patchb->processed_bytes, patchb->processed_bytes + i * 4, patchb);
    if (patchb->enhanced_waitcnt && word == 0xB0A0B570)
      scan_native_flash_reader((const uint8_t*)rom + i * 4, romsize - i * 4,
                               patchb->processed_bytes + i * 4, patchb);
    if (patchb->enhanced_waitcnt && word == 0x0000E012)
      scan_legacy_flashcard((const uint8_t*)rom + i * 4, romsize - i * 4,
                           patchb->processed_bytes + i * 4, patchb);
    if (patchb->enhanced_waitcnt && (word == 0x215522AA || word == 0xB091B530 ||
        word == 0x2603B5F0 || word == 0x2403B5F0 || word == 0xB5F00003 ||
        word == 0x2503B5F0 || word == 0x2580B5F0 || word == 0xB081B580 || word == 0xB093B580 ||
        (word & 0xF800FFFF) == 0x4800B5F0))
      scan_modern_flash((const uint8_t*)rom + i * 4, romsize - i * 4,
                        patchb->processed_bytes + i * 4, patchb);
    if (patchb->enhanced_waitcnt && (word >> 16) == 0xB570)
      scan_prepatched_eeprom((const uint8_t*)rom + i * 4 + 2, romsize - i * 4 - 2,
                             patchb->processed_bytes + i * 4 + 2, patchb);

    // Identify WAITCNT constants, validate them by finding LDR instructions
    if (word == WAITCNT_VALUE_EXACT && !patchb->enhanced_waitcnt) {
      unsigned start_pos_thumb = i < THUMB_LDR_BACKOFF ? 0 : i - THUMB_LDR_BACKOFF;
      unsigned start_pos_arm   = i < ARM_LDR_BACKOFF   ? 0 : i - ARM_LDR_BACKOFF;
      if (find_thumb_ldrpc(rom16, start_pos_thumb * 2, i * 2) ||
          find_arm_ldrpc(rom, start_pos_arm, i)) {
        // This constant seems to be used by an LDR rX, [PC + off], most likely
        // a WAITCNT update. We just patch the constant even tho it's not great
        push_wait_operation(patchb, (patchb->processed_bytes + i * 4) | (OPC_WR_BUF << 28) | (0 << 25));
      }
    }
    // Identify IRQ handle address, so we can find IRQ hook set.
    else if (word == IRQHADDR_VALUE) {
      unsigned start_pos_thumb = i < THUMB_LDR_BACKOFF ? 0 : i - THUMB_LDR_BACKOFF;
      unsigned start_pos_arm   = i < ARM_LDR_BACKOFF   ? 0 : i - ARM_LDR_BACKOFF;
      if (find_thumb_ldrpc(rom16, start_pos_thumb * 2, i * 2) ||
          find_arm_ldrpc(rom, start_pos_arm, i)) {
        // This constant seems to be used by an LDR rX, [PC + off], most likely
        // an IRQ handler write. We just patch the constant to point to the reserved area.
        uint32_t operation = (patchb->processed_bytes + i * 4) | (OPC_WR_BUF << 28) | (1 << 25);
        if (patchb->enhanced_waitcnt) {
          if (scan_index.pending_irq_count == 128) return false;
          scan_index.pending_irq[scan_index.pending_irq_count++] = operation;
        } else if (!push_irq_operations(patchb, &operation, 1))
          return false;
      }
    }

    // Find save strings to narrow down save type.
    else if (word == SRAM_V_WORD0) {
      if (i + 1 < romsize / 4 && (rom[i+1] == SRAM_V_WORD1 || rom[i+1] == SRAM_F_WORD1))
        patchb->save_type_guess |= GUESS_SRAM;
    }
    else if (word == EEPROM_V_WORD0) {
      if (i + 1 < romsize / 4 && rom[i+1] == EEPROM_V_WORD1)
        patchb->save_type_guess |= GUESS_EEPROM;
    }
    else if (word == FLASH_V_WORD0) {
      if (i + 1 < romsize / 4 && rom[i+1] == FLASH_V_WORD1)
        patchb->save_type_guess |= GUESS_FLASH;
      else if (i + 1 < romsize / 4 && rom[i+1] == FLASH512_WORD1)
        patchb->save_type_guess |= GUESS_FLASH64;
      else if (i + 1 < romsize / 4 && rom[i+1] == FLASH1M_WORD1)
        patchb->save_type_guess |= GUESS_FLASH128;
    }
    else if (word == RTC_V_WORD0) {
      if (i + 1 < romsize / 4 && rom[i+1] == RTC_V_WORD1)
        patchb->rtc_guess = true;
    }

    else if (patchb->enhanced_waitcnt && word == 0x0400B570) {
      scan_prepatched_eeprom((const uint8_t*)&rom[i], romsize - i * 4,
                             patchb->processed_bytes + i * 4, patchb);
    }

    // Save function prefix matching.
    else if (word == eeprom_v1_read_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, eeprom_v1_read_sig, sizeof(eeprom_v1_read_sig))) {
        patchb->eeprom_read_types |= 1;
        push_save_handler(patchb, OPC_EEPROM_HD, EEPROM_RD_HNDLR, patchb->processed_bytes + i * 4);
      }
    }
    else if (word == eeprom_v2_read_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, eeprom_v2_read_sig, sizeof(eeprom_v2_read_sig))) {
        patchb->eeprom_read_types |= 2;
        push_save_handler(patchb, OPC_EEPROM_HD, EEPROM_RD_HNDLR, patchb->processed_bytes + i * 4);
      }
    }
    else if (word == eeprom_v1_write_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, eeprom_v1_write_sig, sizeof(eeprom_v1_write_sig)))
        push_save_handler(patchb, OPC_EEPROM_HD, EEPROM_WR_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == eeprom_v2_write_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, eeprom_v2_write_sig, sizeof(eeprom_v2_write_sig)))
        push_save_handler(patchb, OPC_EEPROM_HD, EEPROM_WR_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == eeprom_v3_write_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, eeprom_v3_write_sig, sizeof(eeprom_v3_write_sig)))
        push_save_handler(patchb, OPC_EEPROM_HD, EEPROM_WR_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == eeprom_v4_write_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, eeprom_v4_write_sig, sizeof(eeprom_v4_write_sig)))
        push_save_handler(patchb, OPC_EEPROM_HD, EEPROM_WR_HNDLR, patchb->processed_bytes + i * 4);
    }

    else if (word == flash_bank_word0) {
      if (romsize - i * 4 >= sizeof(flash_bank_sig) &&
          memcmp(&rom[i], flash_bank_sig, sizeof(flash_bank_sig)) == 0)
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_BANK_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == flash_v1_read_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v1_read_sig, sizeof(flash_v1_read_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_READ_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == flash_v23_read_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v2_read_sig, sizeof(flash_v2_read_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_READ_HNDLR, patchb->processed_bytes + i * 4);
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v3_read_sig, sizeof(flash_v3_read_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_READ_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == flash_v1_ident_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v1_ident_sig, sizeof(flash_v1_ident_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_IDEN_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == flash_v2_ident_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v2_ident_sig, sizeof(flash_v2_ident_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_IDEN_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == flash_v1_verify_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v1_verify_sig, sizeof(flash_v1_verify_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_VERF_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == flash_v23_verify_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v2_verify_sig, sizeof(flash_v2_verify_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_VERF_HNDLR, patchb->processed_bytes + i * 4);
      if (match_sig_prefix(&rom[i], romsize - i * 4, flash_v3_verify_sig, sizeof(flash_v3_verify_sig)))
        push_save_handler(patchb, OPC_FLASH_HD, FLASH_VERF_HNDLR, patchb->processed_bytes + i * 4);
    }

    else if (word == siirtc_probe_reset_sig_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, siirtc_probe_sig, sizeof(siirtc_probe_sig)))
        push_rtc_handler(patchb, RTC_PROBE_HNDLR, patchb->processed_bytes + i * 4);
      if (romsize - i * 4 >= 132 && rom[i + 31] == 0xBC80B004 && rom[i + 32] == 0x4708BC02 &&
          match_sig_prefix(&rom[i], romsize - i * 4, siirtc_reset_sync, sizeof(siirtc_reset_sync)))
        push_rtc_handler(patchb, RTC_RESET_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == siirtc_getstatus_sig_word0) {
      if (match_sig_prefix(&rom[i], romsize - i * 4, siirtc_getstatus_sig, sizeof(siirtc_getstatus_sig)))
        push_rtc_handler(patchb, RTC_STSRD_HNDLR, patchb->processed_bytes + i * 4);
    }
    else if (word == siirtc_getdatetime_sig_word0) {
      if (i * 4 >= 504 && romsize - i * 4 >= 176 && rom[i + 42] == 0xBC80B002 && rom[i + 43] == 0x4708BC02 &&
          rom[i - 95] == 0xBC80B004 && rom[i - 94] == 0x4708BC02 &&
          match_sig_prefix(&rom[i - 126], romsize - i * 4 + 504, siirtc_reset_sync, sizeof(siirtc_reset_sync)) &&
          match_sig_prefix(&rom[i], romsize - i * 4, siirtc_getdatetime_sig, sizeof(siirtc_getdatetime_sig))) {
        push_rtc_handler(patchb, RTC_GETTD_HNDLR, patchb->processed_bytes + i * 4);
        if (romsize - i * 4 >= 332 && rom[i + 42] == 0xBC80B002 && rom[i + 43] == 0x4708BC02 &&
            match_sig_prefix(&rom[i + 44], romsize - i * 4 - 176, siirtc_setdatetime_sig, sizeof(siirtc_setdatetime_sig)) &&
            rom[i + 81] == 0xBC80B002 && rom[i + 82] == 0x4708BC02)
          push_rtc_handler(patchb, RTC_SETTD_HNDLR, patchb->processed_bytes + i * 4 + 176);
      }
    }

    else {
      // Try to match FLASH setup info data structure (word aligned)
      const t_flash_setup_info_v1 *info1 = (t_flash_setup_info_v1*)&rom[i];
      const t_flash_setup_info_v2 *info2 = (t_flash_setup_info_v2*)&rom[i];
      if (romsize - i * 4 >= sizeof(*info2) && SEEMS_FLASHINFO(info2) && isromaddr(info2->program_byte_fnptr)) {
        // Validate Device ID and check that sizes make sense.
        if (FLASHINFO_VALIDSIZE(info2) && valid_flashid(info2->device_id)) {
          // Extract handler info from the table.
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_CLRC_HNDLR, 0x1FFFFFE & info2->erase_chip_fnptr);
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_CLRS_HNDLR,
                            flash_sector_patch_target(rom, romsize, patchb->processed_bytes,
                                                      0x1FFFFFE & info2->erase_sector_fnptr, patchb));
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_WRTS_HNDLR, 0x1FFFFFE & info2->program_sector_fnptr);
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_WRBT_HNDLR, 0x1FFFFFE & info2->program_byte_fnptr);
          if (info2->device_id) {
            if (isflash128k(info2->device_id))
              patchb->flash128cnt++;
            else
              patchb->flash64cnt++;
          }
        }
        i += 9;    // Avoid matching with v1 as well, save some time too!
      }
      else if (romsize - i * 4 >= sizeof(*info1) && SEEMS_FLASHINFO(info1)) {
        if (FLASHINFO_VALIDSIZE(info1) && valid_flashid(info1->device_id)) {
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_CLRC_HNDLR, 0x1FFFFFE & info1->erase_chip_fnptr);
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_CLRS_HNDLR,
                            flash_sector_patch_target(rom, romsize, patchb->processed_bytes,
                                                      0x1FFFFFE & info1->erase_sector_fnptr, patchb));
          push_save_handler(patchb, OPC_FLASH_HD, FLASH_WRTS_HNDLR, 0x1FFFFFE & info1->program_sector_fnptr);
          if (info1->device_id) {
            if (isflash128k(info1->device_id))
              patchb->flash128cnt++;
            else
              patchb->flash64cnt++;
          }
        }
        i += 8;
      }
    }

  *position = i;
  return !patchb->failed;
}

ARM_CODE IWRAM_CODE NOINLINE
bool patchengine_process_rom(const uint32_t *rom, unsigned romsize, t_patch_builder *patchb, void(*progresscb)(unsigned)) {
  if (patchb->enhanced_waitcnt && !patchb->processed_bytes && romsize >= 192) {
    memcpy(patchb->reference_key, (const uint8_t*)rom + 0xAC, 4);
    patchb->reference_key[4] = ((const uint8_t*)rom)[0xBC];
  }
  if (patchb->failed || !scan_enhanced_boot(rom, romsize, patchb, progresscb))
    return false;
  if (patchb->enhanced_waitcnt && !prepare_scan_index(romsize, patchb->wait_tail, patchb->wait_tail_size))
    return false;

  if (patchb->enhanced_waitcnt && patchb->wait_tail_size) {
    uint8_t boundary[80];
    unsigned before = MIN(patchb->wait_tail_size, 40);
    unsigned after = MIN(romsize, 40);
    memcpy(boundary, (const uint8_t*)patchb->wait_tail + patchb->wait_tail_size - before, before);
    memcpy(boundary + before, rom, after);
    for (unsigned offset = 0; offset < before; offset += 2) {
      scan_prepatched_eeprom(boundary + offset, before + after - offset,
                             patchb->processed_bytes - before + offset, patchb);
      if (offset + 38 > before)
        scan_native_flash_reader(boundary + offset, before + after - offset,
                                 patchb->processed_bytes - before + offset, patchb);
    }
  }

  for (unsigned i = 0; i < romsize / sizeof(uint32_t); i++) {
    unsigned discovered = i;
    uint32_t word = rom[i];
    if (patchb->enhanced_waitcnt)
      scan_index_word(&scan_index, patchb->wait_tail_size + i * 4, word);
    // Count the number of identical words
    if (patchb->ldata == word)
      patchb->ldatacnt += 4;
    else {
      note_padding_run(patchb, patchb->processed_bytes + i * 4);
      patchb->ldata = word;
      patchb->ldatacnt = 0;
    }

    if (progresscb && !(i & 0x3FFF))
      progresscb(patchb->enhanced_waitcnt ? i / 2 : i);

    if (!main_scan_candidate(word)) continue;
    if (!process_save_candidate(rom, romsize, patchb, &i, word)) return false;
    if (patchb->enhanced_waitcnt) {
      // The legacy save-table matcher skips its own table words. Other
      // families must still inspect them, without changing padding semantics.
      for (unsigned skipped = discovered + 1; skipped <= i && skipped < romsize / 4; skipped++)
        scan_index_word(&scan_index, patchb->wait_tail_size + skipped * 4, rom[skipped]);
    }
    if (patchb->failed)
      return false;
  }

  if (patchb->enhanced_waitcnt) {
    if (!scan_enhanced_candidates(rom, romsize, patchb)) return false;
    if (progresscb) progresscb(3 * (romsize / 16));
    if (!waitscan_process_progress_indexed((const uint8_t*)rom, romsize, patchb->processed_bytes,
          (const uint8_t*)patchb->wait_tail, patchb->wait_tail_size,
          push_wait_operation, patchb, NULL, 0, 1, &scan_index)) return false;
    for (unsigned pending = 0; pending < scan_index.pending_irq_count; pending++)
      if (!push_irq_operations(patchb, scan_index.pending_irq + pending, 1)) return false;
    if (progresscb) progresscb(romsize / 4);
    unsigned aligned_size = romsize & ~3U;
    if (aligned_size >= WAITSCAN_CONTEXT_SIZE) {
      patchb->wait_tail_size = WAITSCAN_CONTEXT_SIZE;
#ifdef __GBA__
      memcpy32(patchb->wait_tail, (const uint8_t*)rom + aligned_size - WAITSCAN_CONTEXT_SIZE, WAITSCAN_CONTEXT_SIZE);
#else
      memcpy(patchb->wait_tail, (const uint8_t*)rom + aligned_size - WAITSCAN_CONTEXT_SIZE, WAITSCAN_CONTEXT_SIZE);
#endif
    }
    else {
      unsigned retained = MIN(patchb->wait_tail_size, WAITSCAN_CONTEXT_SIZE - aligned_size);
      memmove32(patchb->wait_tail, (uint8_t*)patchb->wait_tail + patchb->wait_tail_size - retained, retained);
#ifdef __GBA__
      memcpy32((uint8_t*)patchb->wait_tail + retained, rom, aligned_size);
#else
      memcpy((uint8_t*)patchb->wait_tail + retained, rom, aligned_size);
#endif
      patchb->wait_tail_size = retained + aligned_size;
    }
  }
  patchb->processed_bytes += romsize;
  return true;
}

// Generates a patch buffer (for a file) so that it can be loaded later.
int serialize_patch(const t_patch *patch, uint8_t *buffer) {
  // Write the patch into the buffer.
  memcpy(&buffer[0], "SUPERFWPATCHV01", 16);
  buffer[16] = patch->wcnt_ops;
  buffer[17] = patch->save_ops;
  buffer[18] = patch->save_mode;
  buffer[19] = patch->irqh_ops;
  buffer[20] = patch->rtc_ops;
  buffer[21] = patch->irq_flags;
  buffer[22] = (patch->hole_size >> 10) & 0xFF;
  buffer[23] = patch->hole_size >> 18;
  buffer[24] = (patch->hole_addr >> 10) & 0xFF;
  buffer[25] = patch->hole_addr >> 18;
  buffer[26] = 0;
  buffer[27] = 0;
  memcpy(buffer + 28, &patch->menu_layout, 4);
  buffer += 32;

  memcpy(buffer, patch->prgs, sizeof(patch->prgs));
  buffer += sizeof(patch->prgs);
  memcpy(buffer, patch->op, sizeof(patch->op));

  return 32 + sizeof(patch->op) + sizeof(patch->prgs);
}

// Loads a patch from a buffer.
bool unserialize_patch(const uint8_t *buffer, unsigned size, t_patch *patch) {
  // Check header anf size
  if (size != 32 + sizeof(patch->op) + sizeof(patch->prgs))
    return false;
  if (memcmp(&buffer[0], "SUPERFWPATCHV01", 16))
    return false;

  patch->wcnt_ops = buffer[16];
  patch->save_ops = buffer[17];
  patch->save_mode = buffer[18];
  patch->irqh_ops = buffer[19];
  patch->rtc_ops = buffer[20];
  patch->irq_flags = buffer[21];
  memcpy(&patch->menu_layout, buffer + 28, 4);
  patch->hole_size = (buffer[22] | (buffer[23] << 8)) << 10;
  patch->hole_addr = (buffer[24] | (buffer[25] << 8)) << 10;
  buffer += 32;

  memcpy(patch->prgs, buffer, sizeof(patch->prgs));
  buffer += sizeof(patch->prgs);
  memcpy(patch->op, buffer, sizeof(patch->op));

  return true;
}

bool load_rom_patches(const char *romfn, t_patch *patches) {
  // Look for .patch files next to the ROM.
  char tmp[MAX_FN_LEN];
  strcpy(tmp, romfn);
  replace_extension(tmp, ".patch");

  // Try to open the file and process its content.
  FIL fd;
  if (FR_OK != f_open(&fd, tmp, FA_READ))
    return false;

  uint8_t buf[1024];
  UINT rdbytes;
  if (FR_OK != f_read(&fd, buf, sizeof(buf), &rdbytes))
    return false;

  return unserialize_patch(buf, rdbytes, patches);
}

bool load_cached_patches(const char *romfn, t_patch *patches) {
  char tmp[MAX_FN_LEN];
  const char *p = file_basename(romfn);
  strcpy(tmp, PATCHDB_PATH);
  strcat(tmp, p);

  // Replace the extension of the ROM with something more fitting.
  replace_extension(tmp, ".patch");

  // Try to open the file and process its content.
  FIL fd;
  if (FR_OK != f_open(&fd, tmp, FA_READ))
    return false;

  uint8_t buf[1024];
  UINT rdbytes;
  if (FR_OK != f_read(&fd, buf, sizeof(buf), &rdbytes))
    return false;

  return unserialize_patch(buf, rdbytes, patches);
}

NOINLINE bool write_patches_cache(const char *romfn, const t_patch *patches) {
  char tmp[MAX_FN_LEN];
  const char *p = file_basename(romfn);
  strcpy(tmp, PATCHDB_PATH);
  strcat(tmp, p);

  // Replace the extension of the ROM with something more fitting.
  replace_extension(tmp, ".patch");

  // Attempt to create dirs, should they not exist
  f_mkdir(SUPERFW_DIR);
  f_mkdir(PATCHDB_PATH);
  // Delete any existing patch file
  f_unlink(tmp);

  // Try to open the file and process its content.
  FIL fd;
  if (FR_OK != f_open(&fd, tmp, FA_WRITE | FA_CREATE_ALWAYS))
    return false;

  uint8_t buf[1024];
  int fs = serialize_patch(patches, buf);

  UINT wrbytes;
  if (FR_OK != f_write(&fd, buf, fs, &wrbytes)) {
    f_close(&fd);
    f_unlink(tmp);
    return false;
  }

  f_close(&fd);
  return wrbytes == fs;
}


