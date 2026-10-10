#include "romcompat.h"
#include "compiler.h"
#include "rom_native_readers.h"
#include "rom_eeprom_capacity.h"
#include "common.h"
#include "romidentity_assets.h"
#include "legacy_entry_checks.h"
#ifdef __GBA__
#include "supercard_driver.h"
#endif
#ifndef __GBA__
#include <stdio.h>
#include <stdlib.h>
#endif

#pragma GCC optimize ("Os")

static const uint8_t gba_boot_logo[156] = {
  36,255,174,81,105,154,162,33,61,132,130,10,
  132,228,9,173,17,36,139,152,192,129,127,33,
  163,82,190,25,147,9,206,32,16,70,74,74,
  248,39,49,236,88,199,232,51,130,227,206,191,
  133,244,223,148,206,75,9,193,148,86,138,192,
  19,114,167,252,159,132,77,115,163,202,154,97,
  88,151,163,39,252,3,152,118,35,29,199,97,
  3,4,174,86,191,56,132,0,64,167,14,253,
  255,82,254,3,111,149,48,241,151,251,192,133,
  96,214,128,37,169,99,190,3,1,78,56,226,
  249,162,52,255,187,62,3,68,120,0,144,203,
  136,17,58,148,101,192,124,99,135,240,60,175,
  214,37,228,139,56,10,172,114,33,212,248,7,
};

static uint8_t gba_header_checksum(const uint8_t *header) {
  unsigned sum = 0x19;
  for (unsigned offset = 0xA0; offset < 0xBD; ++offset)
    sum += header[offset];
  return -sum;
}

unsigned romcompat_gba_header_errors(const uint8_t *header, unsigned size) {
  if (!header || size < 192)
    return ROMCOMPAT_GBA_HEADER_SHORT;
  unsigned errors = 0;
  if (memcmp(header + 4, gba_boot_logo, sizeof(gba_boot_logo)))
    errors |= ROMCOMPAT_GBA_LOGO_INVALID;
  if (header[0xB2] != 0x96)
    errors |= ROMCOMPAT_GBA_FIXED_INVALID;
  if (header[0xBD] != gba_header_checksum(header))
    errors |= ROMCOMPAT_GBA_CHECKSUM_INVALID;
  return errors;
}

bool romcompat_fix_gba_header(volatile uint16_t *header, unsigned size) {
  if (!header || ((uintptr_t)header & 1) || size < 192)
    return false;
  const uint8_t *bytes = (const uint8_t*)header;
  unsigned errors = romcompat_gba_header_errors(bytes, size);
  if (!errors)
    return false;
  uint32_t entry = header[0] | ((uint32_t)header[1] << 16);
  int32_t displacement = entry & 0xFFFFFF;
  if (displacement & 0x800000)
    displacement -= 0x1000000;
  int32_t target = 8 + displacement * 4;
  if ((entry >> 24) != 0xEA || target < 192 || (unsigned)target >= MAX_GBA_ROM_SIZE)
    return false;
  if (errors & ROMCOMPAT_GBA_LOGO_INVALID)
    for (unsigned offset = 0; offset < sizeof(gba_boot_logo); offset += 2)
      header[(4 + offset) / 2] = gba_boot_logo[offset] | ((uint16_t)gba_boot_logo[offset + 1] << 8);
  if (errors & ROMCOMPAT_GBA_FIXED_INVALID)
    header[0xB2 / 2] = (header[0xB2 / 2] & 0xFF00) | 0x96;
  uint16_t version = header[0xBC / 2] & 0xFF;
  header[0xBC / 2] = version | ((uint16_t)gba_header_checksum(bytes) << 8);
  return true;
}

bool romcompat_repair_asset_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  if (!hash || !patch || romsize < 192 || romsize > 32 * 1024 * 1024 ||
      patch->save_mode != SaveTypeFlash1024K)
    return false;
#ifdef __GBA__
  t_rom_asset_identity asset_identities[ROM_ASSET_IDENTITY_COUNT];
  set_supercard_mode(MAPPED_SDRAM, true, false);
  const uint8_t *database = (const uint8_t*)ROM_HACKDB_U8;
  unsigned database_size = *(const uint32_t*)(database + 12);
  memcpy(asset_identities, database + database_size - ROM_NATIVE_READER_COUNT * sizeof(t_rom_native_reader) -
         sizeof(asset_identities), sizeof(asset_identities));
  set_supercard_mode(MAPPED_SDRAM, true, true);
#endif
  for (unsigned identity = 0; identity < sizeof(asset_identities) / sizeof(asset_identities[0]); ++identity) {
    if (memcmp(hash, asset_identities[identity].hash, 32))
      continue;
    for (unsigned index = 0; index < asset_identities[identity].count; ++index)
      if (asset_identities[identity].offsets[index] >= romsize)
        return false;
    unsigned added = asset_identities[identity].count * 2;
    unsigned total = patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops;
    if (total + added > MAX_PATCH_OPS)
      return false;
    for (unsigned index = 0; index + 1 < patch->save_ops; ++index)
      if (patch->op[patch->wcnt_ops + index] == (0x30000000 | asset_identities[identity].offsets[0]) &&
          patch->op[patch->wcnt_ops + index + 1] == asset_identities[identity].values[0])
        return false;
    unsigned end = patch->wcnt_ops + patch->save_ops;
    memmove(patch->op + end + added, patch->op + end,
            (patch->irqh_ops + patch->rtc_ops) * sizeof(uint32_t));
    for (unsigned index = 0; index < asset_identities[identity].count; ++index) {
      patch->op[end++] = 0x30000000 | asset_identities[identity].offsets[index];
      patch->op[end++] = asset_identities[identity].values[index];
    }
    patch->save_ops += added;
    return true;
  }
  return false;
}

bool romcompat_note_native_flash_reader(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  if (!hash || !patch || patch->save_mode != SaveTypeFlash1024K)
    return false;
#ifdef __GBA__
  t_rom_native_reader rom_native_readers[ROM_NATIVE_READER_COUNT];
  set_supercard_mode(MAPPED_SDRAM, true, false);
  const uint8_t *database = (const uint8_t*)ROM_HACKDB_U8;
  unsigned database_size = *(const uint32_t*)(database + 12);
  memcpy(rom_native_readers, database + database_size - sizeof(rom_native_readers), sizeof(rom_native_readers));
  set_supercard_mode(MAPPED_SDRAM, true, true);
#endif
  for (unsigned entry = 0; entry < ROM_NATIVE_READER_COUNT; entry++) {
    const t_rom_native_reader *reader = rom_native_readers + entry;
    if (romsize != reader->size || memcmp(hash, reader->hash, 32))
      continue;
    unsigned total = patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops;
    if (total >= MAX_PATCH_OPS)
      return false;
    for (unsigned index = 0; index < patch->save_ops; ) {
      uint32_t operation = patch->op[patch->wcnt_ops + index];
      unsigned opcode = operation >> 28, argument = (operation >> 25) & 7;
      if ((operation & 0x1FFFFFF) == reader->offset &&
          (opcode == PATCH_NATIVE_FLASH_READ_OPCODE || (opcode == 9 && argument == 0)))
        return false;
      index += opcode == 4 ? argument + 2 : opcode == 3 ? 1 + (argument + 4) / 4 : 1;
    }
    unsigned position = patch->wcnt_ops + patch->save_ops;
    memmove(patch->op + position + 1, patch->op + position,
            (patch->irqh_ops + patch->rtc_ops) * sizeof(uint32_t));
    patch->op[position] = PATCH_NATIVE_FLASH_READ_OPCODE << 28 | reader->offset;
    patch->save_ops++;
    return true;
  }
  return false;
}

__attribute__((optimize("Oz"), noinline))
bool romcompat_recover_eeprom_capacity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  if (patch->save_mode != SaveTypeEEPROM64K)
    return false;
#ifdef __GBA__
  set_supercard_mode(MAPPED_SDRAM, true, false);
  const uint8_t *database = (const uint8_t*)ROM_HACKDB_U8;
  unsigned size = ROM_EEPROM_CAPACITY_BYTES;
  const uint8_t *table = database + *(const uint32_t*)(database + 12) - 1064 - ROM_EEPROM_CAPACITY_BYTES;
#else
  static uint8_t *table;
  static unsigned size;
  if (!table) {
    char path[4096];
    const char *source = __FILE__, *separator = strrchr(source, '/');
    const char *windows_separator = strrchr(source, '\\');
    if (!separator || (windows_separator && windows_separator > separator))
      separator = windows_separator;
    if (!separator)
      return false;
    unsigned length = separator - source;
    if (length + 30 >= sizeof(path))
      return false;
    memcpy(path, source, length);
    strcpy(path + length, "/../res/eeprom-capacity.db");
    FILE *file = fopen(path, "rb");
    if (!file)
      return false;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);
    table = malloc(size);
    if (!table || fread(table, 1, size, file) != size) {
      free(table);
      table = NULL;
      fclose(file);
      return false;
    }
    fclose(file);
  }
#endif
  bool found = false;
  for (const uint8_t *entry = table + 4; entry < table + size - 4; entry += 36)
    if (*(const uint32_t*)(entry + 32) == romsize && !memcmp(hash, entry, 32)) {
      found = true;
      break;
    }
#ifdef __GBA__
  set_supercard_mode(MAPPED_SDRAM, true, true);
#endif
  if (found)
    patch->save_mode = SaveTypeEEPROM4K;
  return found;
}

bool romcompat_recover_sram_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  static const uint8_t identity[32] = {
    0x4D,0x91,0x41,0x50,0xF3,0x0A,0x2A,0x31,0x1C,0x42,0x37,0x22,0xDD,0x67,0xBC,0x01,
    0x32,0x77,0xBB,0x9B,0xA6,0x42,0x5B,0x61,0x0B,0x95,0x0E,0xE1,0x96,0x76,0xB6,0xD1
  };
  if (!hash || !patch || romsize != 8 * 1024 * 1024 || memcmp(hash, identity, sizeof(identity)) ||
      patch->save_mode != SaveTypeEEPROM64K || patch->save_ops)
    return false;
  patch->save_mode = SaveTypeSRAM;
  if (!patch->irqh_ops && patch->wcnt_ops + patch->save_ops + patch->rtc_ops <= MAX_PATCH_OPS - 24) {
    // Keep the legacy SRAM/IRQ wrapper, but separate its native vector from
    // the menu vector. 0x03007FF0 is used by this game; use its free 0x7FEC slot.
    static const uint32_t operations[] = {
      0xD07BF7F8,0xE5001004,0x03007FFC,0xD07BF980,0xE5001004,0x03007FFC,
      0x400001E8,0x03007FEC,0x400004AC,0x03007FEC,0x40470448,0x03FFFFEC,
      0x407BF8FC,0x1510F014,0x407BF938,0xE510F014,0x407BF944,0x0510F014,
      0x407BF954,0x1510F014,0x407BF988,0xE510F014,0x327BF8EC,0x00006048
    };
    unsigned position = patch->wcnt_ops + patch->save_ops;
    memmove(patch->op + position + 24, patch->op + position, patch->rtc_ops * 4);
    memcpy(patch->op + position, operations, sizeof(operations));
    patch->irqh_ops = 24;
    patch->irq_flags |= IRQPATCH_BOOT_RESTORE;
  }
  return true;
}

bool romcompat_recover_flash_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  static const uint8_t identities[][32] = {
    {0x1E,0x1C,0x45,0x78,0x2C,0xFC,0xE2,0x20,0xCE,0x29,0x2E,0x98,0xEF,0x0B,0x68,0x6F,
     0x23,0x03,0xA6,0x74,0x7B,0x4C,0xA0,0xC6,0x02,0x01,0xAB,0xD2,0x51,0xEF,0xAA,0x63},
    {0xF3,0x62,0x21,0x29,0x47,0x1E,0x1B,0xF4,0x2C,0x3C,0x39,0x25,0x94,0xB1,0xE2,0x14,
     0x8E,0xF3,0x00,0xCC,0x5F,0xE3,0xA3,0x67,0x82,0xF2,0xBB,0xEC,0x87,0x31,0x9D,0x61}
  };
  static const uint8_t identifier_stub[] = {0x09,0x20,0x00,0x02,0xC2,0x30,0x70,0x47};
  if (!hash || !patch || romsize != 32 * 1024 * 1024 ||
      (memcmp(hash, identities[0], 32) && memcmp(hash, identities[1], 32)) ||
      patch->save_mode != SaveTypeSRAM || patch->save_ops ||
      patch->hole_addr != 0x012E2400 || patch->hole_size != 0x00D1DC00 ||
      patch->prgs[3].length != sizeof(identifier_stub) ||
      memcmp(patch->prgs[3].data, identifier_stub, sizeof(identifier_stub)))
    return false;
  const uint32_t recovered[] = {
    0x092E2408,0x092E2438,
    0x081DEFA5,0x081DF071,0x081DEE61,0x081DEED5,0x081DEDC1,0x092E2468,
    0x00020000,0x00001000,0x0020000C,0,0x00010003,0x000009C2,
    0x081DEFA5,0x081DF071,0x081DEE61,0x081DEED5,0x081DEDC1,0x092E2468,
    0x00020000,0x00001000,0x0020000C,0,0x00010003,0,
    0xFFBD000A,0x000A00C2,0x00C2FFBD,0xFFBD07D0,0x07D000C2,0x00C2FFBD
  };
  uint32_t operations[45] = {
    0x9A1DE820,0x061DE844,0x901DEA98,0x921DEE60,0x941DEED4,0x961DF070,0x981DEFA4,
    0x401DED48,0x092E2400
  };
  unsigned count = 9;
  for (unsigned offset = 0; offset < 32; offset += 8) {
    operations[count++] = 0x4E000000 | (0x012E2400 + offset * 4);
    for (unsigned index = 0; index < 8; ++index)
      operations[count++] = recovered[offset + index];
  }
  unsigned total = patch->wcnt_ops + patch->irqh_ops + patch->rtc_ops;
  if (total + count > MAX_PATCH_OPS)
    return false;
  memmove(patch->op + patch->wcnt_ops + count, patch->op + patch->wcnt_ops,
          (patch->irqh_ops + patch->rtc_ops) * sizeof(uint32_t));
  memcpy(patch->op + patch->wcnt_ops, operations, sizeof(operations));
  patch->save_mode = SaveTypeFlash1024K;
  patch->save_ops = count;
  patch->hole_addr += 1024;
  patch->hole_size -= 1024;
  return true;
}

bool romcompat_guard_window_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  static const uint8_t identities[][32] = {
    {0x22,0x8a,0x04,0xe9,0x74,0x99,0xda,0x45,0x9c,0x6b,0xe6,0x39,0x88,0xc2,0x01,0x25,0xdc,0xe4,0x17,0x96,0xcd,0x10,0xaf,0xbc,0x92,0x5c,0x0b,0x38,0x80,0x15,0x33,0x7c},
    {0x54,0x23,0xcd,0x6b,0x3d,0xad,0x55,0xca,0x0d,0xab,0x28,0x7c,0x1e,0xfd,0xbb,0xfb,0x57,0x34,0xaa,0x3c,0x45,0xfa,0x26,0xf6,0x13,0x5d,0xe3,0xf8,0xaf,0xd1,0x4e,0xed},
    {0x86,0x49,0xfa,0xac,0xbc,0x54,0x2c,0x91,0xc0,0x70,0xd8,0xe7,0xc5,0x04,0x71,0x3d,0x9c,0xc3,0xbf,0xc1,0xc2,0x2b,0x98,0x2d,0xb5,0x52,0xe5,0xd2,0x84,0x4b,0xde,0xd2},
    {0xcf,0x98,0x15,0xee,0x4d,0x1f,0xb4,0xc2,0x70,0xfa,0x04,0x0e,0x03,0x97,0x11,0x6b,0x0c,0xbf,0x19,0xd7,0x65,0x70,0xe9,0x75,0xe5,0xc5,0xe4,0x17,0xc6,0x37,0x58,0xa4},
  };
  if (!hash || !patch || romsize != 32 * 1024 * 1024 ||
      patch->save_mode != SaveTypeFlash1024K || patch->hole_addr != 0x013AAC00 ||
      patch->hole_size != 0x00C55400)
    return false;
  bool matched = false;
  for (unsigned identity = 0; identity < sizeof(identities) / sizeof(identities[0]); ++identity)
    matched |= memcmp(hash, identities[identity], 32) == 0;
  if (!matched)
    return false;
  uint32_t operations[] = {
    0x42003E3C,0x47184B00,0x08000001 + patch->hole_addr,
    0x4E000000 | patch->hole_addr,
    0x0E000600,0xD10028FF,0xB5F04770,0x0E060600,0x4B024901,0x46C04718,
    0x020204B4,0x08003E45
  };
  unsigned added = sizeof(operations) / sizeof(operations[0]);
  unsigned total = patch->wcnt_ops + patch->save_ops + patch->irqh_ops + patch->rtc_ops;
  if (total + added > MAX_PATCH_OPS)
    return false;
  unsigned end = patch->wcnt_ops + patch->save_ops;
  memmove(patch->op + end + added, patch->op + end,
          (patch->irqh_ops + patch->rtc_ops) * sizeof(uint32_t));
  memcpy(patch->op + end, operations, sizeof(operations));
  patch->save_ops += added;
  patch->hole_addr += 1024;
  patch->hole_size -= 1024;
  return true;
}

static uint32_t read_word(const uint8_t *buffer, unsigned offset) {
  return *(const uint32_t*)&buffer[offset];
}

static uint32_t entry_view_word(const uint8_t *data, const uint8_t *prefix, unsigned prefix_size, int offset) {
  return offset < 0 ? read_word(prefix, prefix_size + offset) : read_word(data, offset);
}

static __attribute__((optimize("Oz"), noinline))
unsigned process_entry_checks(const uint8_t *data, unsigned size, unsigned base,
                              const uint8_t *prefix, unsigned prefix_size, uint32_t original_entry,
                              uint32_t patched_entry,
                              bool (*emit)(void *, const uint32_t *, unsigned), void *context, const t_scan_index *index) {
  if ((original_entry >> 24) != 0xEA)
    return 0;
  unsigned found = 0;
  for (unsigned candidate = 0, position; size + prefix_size >= 32 &&
       scan_index_next(index, SCAN_ENTRY, &candidate, 4, size + prefix_size - 32, &position); ) {
    int offset = (int)position - (int)prefix_size;
    if (base + offset < 192 || entry_view_word(data, prefix, prefix_size, offset) != 0xE92D0007)
      continue;
    uint32_t address_load = entry_view_word(data, prefix, prefix_size, offset + 4);
    uint32_t expected_load = entry_view_word(data, prefix, prefix_size, offset + 12);
    if ((address_load & 0xFFFFF000) != 0xE59F0000 ||
        entry_view_word(data, prefix, prefix_size, offset + 8) != 0xE4B01000 ||
        (expected_load & 0xFFFFF000) != 0xE59F2000 ||
        entry_view_word(data, prefix, prefix_size, offset + 16) != 0xE1520001 ||
        entry_view_word(data, prefix, prefix_size, offset + 20) != 0x0A000000 ||
        entry_view_word(data, prefix, prefix_size, offset + 24) != 0xEF000000 ||
        entry_view_word(data, prefix, prefix_size, offset + 28) != 0xE8BD0007)
      continue;
    int address = offset + 12 + (address_load & 0xFFF);
    int expected = offset + 20 + (expected_load & 0xFFF);
    if ((address | expected) & 3 || address < offset + 32 || expected < offset + 32 ||
        address > (int)size - 4 || expected > (int)size - 4 || address == expected ||
        (offset < 0 && address + 4 <= 0 && expected + 4 <= 0) ||
        entry_view_word(data, prefix, prefix_size, address) != 0x08000000 ||
        entry_view_word(data, prefix, prefix_size, expected) != original_entry)
      continue;
    uint32_t operation = IRQPATCH_METADATA_OPCODE << 28 | IRQPATCH_ENTRY_CHECK_ARGUMENT << 25 | (base + expected);
    if (emit) {
      if (!emit(context, &operation, 1))
        return UINT32_MAX;
    } else {
      volatile uint16_t *destination = (volatile uint16_t*)(data + expected);
      destination[0] = patched_entry;
      destination[1] = patched_entry >> 16;
    }
    found++;
  }
  return found;
}

bool romcompat_scan_entry_checks(const uint8_t *data, unsigned size, unsigned base,
                                const uint8_t *prefix, unsigned prefix_size, uint32_t original_entry,
                                bool (*emit)(void *, const uint32_t *, unsigned), void *context) {
  return process_entry_checks(data, size, base, prefix, prefix_size, original_entry, 0, emit, context, NULL) != UINT32_MAX;
}

bool romcompat_scan_entry_checks_indexed(const uint8_t *data, unsigned size, unsigned base,
    const uint8_t *prefix, unsigned prefix_size, uint32_t original_entry,
    bool (*emit)(void *, const uint32_t *, unsigned), void *context, const t_scan_index *index) {
  return process_entry_checks(data, size, base, prefix, prefix_size, original_entry, 0, emit, context, index) != UINT32_MAX;
}

unsigned romcompat_retarget_entry_check(uint8_t *buffer, unsigned bufsize,
                                      uint32_t original_entry, uint32_t patched_entry) {
  return romcompat_retarget_entry_check_block(buffer, bufsize, 0, original_entry, patched_entry);
}

unsigned romcompat_retarget_entry_check_block(uint8_t *buffer, unsigned bufsize, uint32_t base,
                                            uint32_t original_entry, uint32_t patched_entry) {
  unsigned begin = base < 0xC0 ? 0xC0 - base : 0;
  if ((base & 3) || bufsize < 32 || begin > bufsize - 32 || original_entry == patched_entry ||
      (original_entry >> 24) != 0xEA || (patched_entry >> 24) != 0xEA)
    return 0;

  return process_entry_checks(buffer, bufsize, base, NULL, 0, original_entry, patched_entry, NULL, NULL, NULL);
}

// Legacy files have no entry-check metadata. Check only catalogued code sites,
// retaining the full instruction/literal guard; never search the ROM at launch.
// New/relocated guards are discovered by explicit enhanced patch generation.
unsigned romcompat_apply_legacy_entry_checks(uint8_t *buffer, unsigned size, uint32_t base,
                                            uint32_t original_entry, uint32_t patched_entry) {
  unsigned changed = 0;
  for (unsigned index = 0; index < sizeof(legacy_entry_checks) / sizeof(legacy_entry_checks[0]); index++) {
    if (legacy_entry_checks[index].entry != original_entry || legacy_entry_checks[index].offset < base)
      continue;
    unsigned offset = legacy_entry_checks[index].offset - base;
    unsigned length = legacy_entry_checks[index].size;
    if (offset > size || length > size - offset)
      continue;
    changed += romcompat_retarget_entry_check_block(buffer + offset, length, base + offset,
                                                    original_entry, patched_entry);
  }
  return changed;
}
