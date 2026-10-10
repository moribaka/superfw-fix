#ifndef ROMCOMPAT_H
#define ROMCOMPAT_H

#include <stdint.h>
#include "patchengine.h"

enum {
  ROMCOMPAT_GBA_LOGO_INVALID = 1,
  ROMCOMPAT_GBA_FIXED_INVALID = 2,
  ROMCOMPAT_GBA_CHECKSUM_INVALID = 4,
  ROMCOMPAT_GBA_HEADER_SHORT = 8,
};

unsigned romcompat_gba_header_errors(const uint8_t *header, unsigned size);
bool romcompat_fix_gba_header(volatile uint16_t *header, unsigned size);

bool romcompat_recover_flash_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool romcompat_recover_sram_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool romcompat_note_native_flash_reader(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool romcompat_recover_eeprom_capacity(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool romcompat_repair_asset_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool romcompat_guard_window_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool romcompat_scan_entry_checks(const uint8_t *data, unsigned size, unsigned base,
                                const uint8_t *prefix, unsigned prefix_size, uint32_t original_entry,
                                bool (*emit)(void *, const uint32_t *, unsigned), void *context);

bool romcompat_scan_entry_checks_indexed(const uint8_t *data, unsigned size, unsigned base,
    const uint8_t *prefix, unsigned prefix_size, uint32_t original_entry,
    bool (*emit)(void *, const uint32_t *, unsigned), void *context, const t_scan_index *index);

unsigned romcompat_retarget_entry_check(uint8_t *buffer, unsigned bufsize,
                                      uint32_t original_entry, uint32_t patched_entry);
unsigned romcompat_retarget_entry_check_block(uint8_t *buffer, unsigned bufsize, uint32_t base,
                                            uint32_t original_entry, uint32_t patched_entry);
unsigned romcompat_apply_legacy_entry_checks(uint8_t *buffer, unsigned size, uint32_t base,
                                            uint32_t original_entry, uint32_t patched_entry);

#endif
