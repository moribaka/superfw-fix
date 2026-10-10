// Snezziboy's single-ROM map format is derived from bubble2k's GPL-2.0-or-later
// builder. The source and license are retained under emu/snezziboy-source.
#include <string.h>
#include "sfc.h"
#include "sfc_core_layout.h"
#include "common.h"
#include "util.h"
#include "gbahw.h"
#pragma GCC optimize ("Oz")

static unsigned half(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }


__attribute__((noinline)) bool sfc_inspect(FIL *file, unsigned file_size, t_sfc_info *info) {
  memset(info, 0, sizeof(*info));
  info->source_skip = (file_size & 32767) == 512 ? 512 : 0;
  info->size = file_size - info->source_skip;
  if (info->size < 32768 || info->size > 4*1024*1024 || (info->size & 32767)) return false;
  unsigned best = 0;
  for (unsigned hi = 0; hi < 2; hi++) {
    unsigned offset = hi ? 0xFFC0 : 0x7FC0;
    if (offset + 64 > info->size) continue;
    uint8_t header[64]; UINT read;
    if (f_lseek(file, offset + info->source_skip) != FR_OK ||
        f_read(file, header, sizeof(header), &read) != FR_OK || read != sizeof(header)) return false;
    unsigned score = ((half(header+28) ^ half(header+30)) == 65535 ? 8 : 0) +
                     (half(header+60) >= 0x8000 ? 4 : 0), printable = 0;
    for (unsigned i = 0; i < 21; i++) printable += header[i] >= 32 && header[i] < 127;
    score += printable / 5;
    if (header[22] > 2 || header[24] > 3 || score < 8 || score <= best) continue;
    best = score; info->lorom = !hi;
    memcpy(info->header, header, sizeof(header));
  }
  if (!best) return false;
  info->padded_size = 32768;
  while (info->padded_size < info->size) info->padded_size *= 2;
  info->sram_mask = info->header[24] ? (1024U << info->header[24]) - 1 : 0;
  return true;
}

__attribute__((noinline)) bool sfc_prepare(uint8_t *core, unsigned core_size, t_sfc_info *info) {
  if (core_size != SNEZZI_CORE_SIZE || memcmp(core + SNEZZI_MAP_OFFSET - 8, "SMEMMAP0", 8)) return false;
  info->rom_offset = SNEZZI_ROM_OFFSET;
  uint32_t *map = (uint32_t*)(core + SNEZZI_MAP_OFFSET);
  for (uint32_t chunk = 0; chunk < 2048; chunk++) {
    uint32_t v = chunk * 8192, bank = chunk / 8, part = chunk & 7, target = 0;
    bool regular = bank < 0x40 || (bank >= 0x80 && bank < 0xC0);
    if (bank == 0x7E || bank == 0x7F) target = 0x02000000 + (bank-0x7E)*65536 + (v & 65535);
    else if (regular && !part) target = 0x02000000 + (v & 65535);
    else if (regular && (part == 1 || part == 2)) target = 0x80000000 + (v & 65535);
    else if ((regular && (bank & 0x7F) >= 0x30 && part == 3) ||
             (bank >= 0x70 && bank <= 0x7D && part < 4)) target = 0x80006000 + (v & 8191);
    else if (part >= 4 || bank >= 0xC0 || (!info->lorom && bank >= 0x40 && bank <= 0x6F)) {
      unsigned offset = info->lorom ? (v & 32767) + ((v >> 1) & ((info->padded_size-1) & ~32767U)) :
                                     (v & 65535) + (v & ((info->padded_size-1) & ~65535U));
      target = 0x08000000 + info->rom_offset + offset;
    }
    map[chunk] = target - v;
  }
  map[2048] = info->sram_mask;
  map[2049] = 0x08000000 + info->rom_offset;
  memset32(core + ROUND_UP2(core_size, 4), 0xFFFFFFFF, info->rom_offset - ROUND_UP2(core_size, 4));
  return true;
}

static unsigned mirror(unsigned address, unsigned size) {
  unsigned base = 0;
  while (address >= size) {
    unsigned bit = 0x400000;
    while (!(address & bit)) bit >>= 1;
    address -= bit;
    if (size > bit) { size -= bit; base += bit; }
  }
  return base + address;
}

static void patch_byte(uint8_t *destination, uint8_t value) {
#ifdef __GBA__
  uintptr_t address = (uintptr_t)destination;
  volatile uint16_t *p = (volatile uint16_t*)(address & ~(uintptr_t)1);
  unsigned shift = (address & 1) * 8;
  *p = (*p & ~(255U << shift)) | ((unsigned)value << shift);
#else
  *destination = value;
#endif
}

__attribute__((noinline)) bool sfc_finish(uint8_t *core, const t_sfc_info *info) {
  uint8_t *rom = core + info->rom_offset;
  for (unsigned offset = info->size; offset < info->padded_size; ) {
    unsigned source = mirror(offset, info->size), bytes = MIN(512, info->padded_size-offset);
    // ROM sizes and mirrors are aligned to 32 KiB; DMA always uses whole words.
    dma_memcpy32(rom + offset, rom + source, bytes / 4);
    offset += bytes;
  }
  const uint8_t *database = get_vfile_ptr("SFPT");
  if (!database || memcmp(database, "SFP1", 4)) return true;
  const uint32_t *head = (const uint32_t*)database;
  for (unsigned record = 0; record < head[1]; record++) {
    const uint8_t *entry = database + 8 + record*76;
    if (*(const uint32_t*)entry != info->size || memcmp(entry+4, info->header, 64)) continue;
    unsigned count = *(const uint32_t*)(entry+68), position = *(const uint32_t*)(entry+72);
    // Validate every original instruction context before changing any byte.
    for (unsigned pass = 0; pass < 2; pass++) {
      unsigned cursor = position;
      for (unsigned index = 0; index < count; index++) {
        const uint32_t *op = (const uint32_t*)(database+cursor);
        unsigned offset=op[0], start=op[1], guard=op[2], length=op[3];
        // Catalog offsets are checked by the offline builder. This immutable
        // firmware asset only contains ranges valid for its exact ROM size.
        const uint8_t *expected=database+cursor+16, *value=expected+guard;
        if (!pass && memcmp(rom+start, expected, guard)) return true; // no matching patch profile
        if (pass) for (unsigned byte=0; byte<length; byte++) patch_byte(rom+offset+byte,value[byte]);
        cursor=ROUND_UP2(cursor+16+guard+length,4);
      }
    }
    return true;
  }
  return true;
}
