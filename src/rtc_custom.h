#ifndef RTC_CUSTOM_H
#define RTC_CUSTOM_H

#include "patchengine.h"

#define RTC_CUSTOM_SLOT_SIZE 1024
#define RTC_CUSTOM_MARKER 0x6E000000U

typedef struct {
  uint8_t hash[32];
  uint32_t ready;
  uint32_t status;
  uint8_t count;
  struct { uint32_t offset; uint32_t kind; } functions[7];
} t_rtc_custom_identity;

extern const uint8_t rtc_custom_payload[];
extern const uint32_t rtc_custom_payload_size;

bool rtc_custom_note_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch);
bool rtc_custom_supported(const t_patch *patch);
unsigned patch_rom_size(unsigned size, const t_patch *patch, bool rtc);
void rtc_custom_apply(uint8_t *buffer, unsigned size, unsigned base, const t_patch *patch);

#endif
