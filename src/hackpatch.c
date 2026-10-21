#include "hackpatch.h"
#include "common.h"
#include "sha256.h"
#include "util.h"
#include "romcompat.h"
#include "rtc_custom.h"
#include "fatfs/ff.h"
#include "supercard_driver.h"
#include "gbahw.h"

#pragma GCC optimize ("Os")

static bool apply_save_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  static const uint8_t driver_hash[32] = {0xa9, 0x0e, 0x03, 0x84, 0x88, 0x66, 0xf5, 0xd6, 0x5d, 0x7a, 0xcf, 0x2d, 0xca, 0xcf, 0x53, 0x3b, 0x49, 0xee, 0xa7, 0x1d, 0x2a, 0x7c, 0x45, 0x7a, 0xa3, 0xa6, 0xfd, 0xa6, 0x38, 0x39, 0x19, 0xb9};
  static const uint8_t fusion_hash[32] = {0xbc, 0xfd, 0x95, 0x6f, 0x5d, 0x25, 0x79, 0x16, 0x90, 0x21, 0x83, 0xa7, 0x64, 0x48, 0xba, 0xf3, 0x7f, 0xe7, 0x38, 0x4e, 0x4c, 0xa4, 0xa0, 0x68, 0xc2, 0x2c, 0x47, 0xe4, 0xb3, 0x33, 0xaa, 0x7e};
  if (hash && patch && romsize == 8388608 && !memcmp(hash, fusion_hash, 32)) {
    if (patch->wcnt_ops + patch->save_ops + patch->rtc_ops > MAX_PATCH_OPS - 3)
      return false;
    // Native VBlank callback: keep HBlank dispatch free of menu latency.
    unsigned position = patch->wcnt_ops + patch->save_ops;
    memmove32(patch->op + position + 3, patch->op + position + patch->irqh_ops, patch->rtc_ops * 4);
    patch->op[position] = IRQPATCH_METADATA_OPCODE << 28 | IRQPATCH_VBLANK_ARGUMENT << 25 | 0x0A7558;
    patch->op[position + 1] = 0x08000B8D;
    patch->op[position + 2] = 0x00780000;
    patch->irqh_ops = 3;
    patch->irq_flags = IRQPATCH_ENTRY_CHECKS_SCANNED;
    return true;
  }
  if (hash && patch && romsize == 8407290 && !memcmp(hash, driver_hash, 32)) {
    for (unsigned index = 0; index + 1 < patch->wcnt_ops; index++)
      if (patch->op[index] == 0x320001E8 && patch->op[index + 1] == 0x4000)
        return true;
    unsigned start = patch->wcnt_ops;
    unsigned rest = patch->save_ops + patch->irqh_ops + patch->rtc_ops;
    if (start + rest > MAX_PATCH_OPS - 2)
      return false;
    memmove32(patch->op + start + 2, patch->op + start, rest * 4);
    patch->op[start] = 0x320001E8;
    patch->op[start + 1] = 0x4000;
    patch->wcnt_ops += 2;
    return true;
  }
  if (romcompat_recover_eeprom_capacity(romsize, hash, patch))
    return true;
  bool native_reader = romcompat_note_native_flash_reader(romsize, hash, patch);
  if (romcompat_recover_sram_identity(romsize, hash, patch))
    return true;
  if (romcompat_recover_flash_identity(romsize, hash, patch))
    return true;
  if (romcompat_repair_asset_identity(romsize, hash, patch))
    return true;
  if (romcompat_guard_window_identity(romsize, hash, patch))
    return true;
  static const uint8_t sango_hash[32] = {
    0xF2, 0xCD, 0x23, 0xE8, 0x66, 0x9C, 0xD1, 0x64,
    0x88, 0x06, 0xFC, 0xCB, 0xD1, 0x7E, 0x8C, 0xCB,
    0x10, 0x83, 0x73, 0x25, 0xEB, 0x39, 0x1A, 0x40,
    0x2A, 0x8D, 0xD1, 0x2B, 0x1E, 0xCD, 0x9A, 0xA5
  };
  if (!hash || !patch || romsize != 4 * 1024 * 1024 ||
      memcmp(hash, sango_hash, sizeof(sango_hash)) ||
      patch->save_mode != SaveTypeNone || patch->save_ops)
    return native_reader;
  patch->save_mode = SaveTypeSRAM;
  return true;
}

bool hackpatch_apply_save_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  if (!hash || !patch)
    return false;
  bool applied = apply_save_identity(romsize, hash, patch);
  return rtc_custom_note_identity(romsize, hash, patch) || applied;
}

static void store_word(uint8_t *buffer, unsigned offset, uint32_t value) {
  for (unsigned index = 0; index < 4; index++)
    buffer[offset + index] = value >> (index * 8);
}

static uint32_t read_word(const uint8_t *buffer, unsigned offset) {
  uint32_t value = 0;
  for (unsigned index = 0; index < 4; index++)
    value |= (uint32_t)buffer[offset + index] << (index * 8);
  return value;
}

int hackpatch_serialize(const t_patch *patch, unsigned romsize, const uint8_t hash[32], uint8_t *buffer) {
  memcpy(buffer, "SUPERFWHACKV001", 16);
  store_word(buffer, 16, HACKPATCH_REVISION);
  store_word(buffer, 20, romsize);
  if (hash)
    memcpy(buffer + 24, hash, 32);
  else
    memset(buffer + 24, 0, 32);
  int size = serialize_patch(patch, buffer + 88);
  sha256sum(buffer + 88, size, buffer + 56);
  return 88 + size;
}

bool hackpatch_unserialize(const uint8_t *buffer, unsigned size, unsigned romsize,
                           const uint8_t hash[32], t_patch *patch) {
  if (size != HACKPATCH_SERIALIZED_SIZE || memcmp(buffer, "SUPERFWHACKV001", 16) ||
      read_word(buffer, 16) != HACKPATCH_REVISION || read_word(buffer, 20) != romsize ||
      (hash && memcmp(buffer + 24, hash, 32)))
    return false;
  return unserialize_patch(buffer + 88, size - 88, patch);
}

bool hackpatch_hash_rom(const char *filename, unsigned romsize, uint8_t hash[32],
                        void (*progress)(unsigned, unsigned)) {
  FIL file;
  if (!romsize || f_open(&file, filename, FA_READ) != FR_OK)
    return false;
  bool success = f_size(&file) == romsize;
  SHA256_State state;
  sha256_init(&state);
  for (unsigned offset = 0; success && offset < romsize; ) {
    uint32_t buffer[1024];
    UINT count, requested = MIN(sizeof(buffer), romsize - offset);
    success = f_read(&file, buffer, requested, &count) == FR_OK && count == requested;
    if (success) {
      sha256_transform(&state, buffer, count);
      offset += count;
      if (progress && (!(offset & 0xFFFF) || offset == romsize))
        progress(offset >> 8, (romsize + 255) >> 8);
    }
  }
  if (f_close(&file) != FR_OK)
    success = false;
  if (success)
    sha256_finalize(&state, hash);
  return success;
}

static bool cache_path(const char *filename, char *path) {
  const char *basename = file_basename(filename);
  if (strlen(PATCHDB_PATH) + strlen(basename) + 6 >= MAX_FN_LEN)
    return false;
  strcpy(path, PATCHDB_PATH);
  strcat(path, basename);
  strcat(path, ".hack");
  return true;
}

bool hackpatch_load(const char *filename, unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  char path[MAX_FN_LEN];
  FIL file;
  if (!cache_path(filename, path) || f_open(&file, path, FA_READ) != FR_OK)
    return false;
  uint8_t buffer[HACKPATCH_SERIALIZED_SIZE];
  UINT count = 0;
  bool success = f_size(&file) == sizeof(buffer) &&
                 f_read(&file, buffer, sizeof(buffer), &count) == FR_OK && count == sizeof(buffer);
  if (f_close(&file) != FR_OK)
    success = false;
  return success && hackpatch_unserialize(buffer, count, romsize, hash, patch);
}

bool hackpatch_load_cached(const char *filename, unsigned romsize, t_patch *patch) {
  return hackpatch_load(filename, romsize, NULL, patch);
}

bool hackpatch_quick_id(const char *filename, const uint8_t *header, const uint32_t *offsets,
                        unsigned count, uint32_t *identity) {
  FIL file;
  if (f_open(&file, filename, FA_READ) != FR_OK)
    return false;
  SHA256_State state;
  sha256_init(&state);
  sha256_transform(&state, header, 256);
  bool success = true;
  uint32_t buffer[8];
  for (unsigned index = 0; success && index < count; index++) {
    UINT bytes;
    success = f_lseek(&file, offsets[index]) == FR_OK &&
              f_read(&file, buffer, sizeof(buffer), &bytes) == FR_OK && bytes == sizeof(buffer);
    if (success)
      sha256_transform(&state, buffer, sizeof(buffer));
  }
  if (f_close(&file) != FR_OK)
    success = false;
  if (success) {
    uint8_t hash[32];
    sha256_finalize(&state, hash);
    *identity = read_word(hash, 0);
  }
  return success;
}

bool hackpatch_db_samples(const uint8_t *database, unsigned romsize, uint32_t header_identity,
                          uint32_t *offsets, unsigned *count) {
  if (memcmp(database, "HDB2", 4) || read_word(database, 4) != HACKPATCH_REVISION)
    return false;
  unsigned directory = read_word(database, 16);
  unsigned low = 0, high = read_word(database, directory);
  while (low < high) {
    unsigned middle = low + (high - low) / 2;
    const uint8_t *entry = database + directory + 4 + middle * 16;
    unsigned entry_size = read_word(entry, 0), entry_identity = read_word(entry, 4);
    if (entry_size < romsize || (entry_size == romsize && entry_identity < header_identity)) {
      low = middle + 1;
      continue;
    }
    if (entry_size != romsize || entry_identity != header_identity) {
      high = middle;
      continue;
    }
    *count = read_word(entry, 8);
    unsigned offset = read_word(entry, 12);
    memcpy(offsets, database + offset, *count * 4);
    return true;
  }
  return false;
}

bool hackpatch_db_lookup(const uint8_t *database, unsigned romsize, uint32_t identity, t_patch *patch) {
  if (memcmp(database, "HDB2", 4) || read_word(database, 4) != HACKPATCH_REVISION)
    return false;
  unsigned count = read_word(database, 8);
  unsigned low = 0, high = count;
  while (low < high) {
    unsigned middle = low + (high - low) / 2;
    const uint8_t *entry = database + 20 + middle * 12;
    unsigned entry_size = read_word(entry, 0), entry_identity = read_word(entry, 4);
    if (entry_size < romsize || (entry_size == romsize && entry_identity < identity)) {
      low = middle + 1;
      continue;
    }
    if (entry_size != romsize || entry_identity != identity) {
      high = middle;
      continue;
    }
    unsigned offset = read_word(entry, 8);
    const uint8_t *record = database + offset;
    unsigned operations = record[0] + record[1] + record[3] + record[4];
    memset(patch, 0, sizeof(*patch));
    patch->wcnt_ops = record[0];
    patch->save_ops = record[1];
    patch->save_mode = record[2];
    patch->irqh_ops = record[3];
    patch->rtc_ops = record[4];
    patch->irq_flags = record[5];
    patch->hole_addr = read_word(record, 8);
    patch->hole_size = read_word(record, 12);
    unsigned extra = record[7] ? 4 : 0;
    if (extra) patch->menu_layout = read_word(record, 16);
    memcpy(patch->op, record + 16 + extra, operations * 4);
    offset += 16 + extra + operations * 4;
    for (unsigned program = 0; program < record[6]; program++) {
      unsigned length = database[offset++];
      patch->prgs[program].length = length;
      memcpy(patch->prgs[program].data, database + offset, length);
      offset += length;
    }
    return true;
  }
  return false;
}

bool hackpatch_save(const char *filename, unsigned romsize, const uint8_t hash[32], const t_patch *patch) {
  char path[MAX_FN_LEN];
  if (!cache_path(filename, path))
    return false;
  f_mkdir(SUPERFW_DIR);
  f_mkdir(PATCHDB_PATH);
  FIL file;
  if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    return false;
  uint8_t buffer[HACKPATCH_SERIALIZED_SIZE];
  unsigned size = hackpatch_serialize(patch, romsize, hash, buffer);
  UINT count;
  bool success = f_write(&file, buffer, size, &count) == FR_OK && count == size;
  if (f_close(&file) != FR_OK)
    success = false;
  if (!success)
    f_unlink(path);
  return success;
}
