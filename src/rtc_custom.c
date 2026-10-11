#include "rtc_custom.h"
#include "rtc_custom_identities.h"
#include "common.h"
#include "patches.h"

#pragma GCC optimize ("Oz")

static uint32_t word(const uint8_t *data, unsigned offset) {
#ifdef __GBA__
  // All payload words and relocation entries are four-byte aligned.
  return *(const uint32_t*)(data+offset);
#else
  uint32_t value;
  memcpy(&value, data + offset, 4);
  return value;
#endif
}

bool rtc_custom_note_identity(unsigned romsize, const uint8_t hash[32], t_patch *patch) {
  if (romsize != MAX_GBA_ROM_SIZE || patch->rtc_ops || patch->hole_size < RTC_CUSTOM_SLOT_SIZE)
    return false;
  for (unsigned index = 0; index < sizeof(rtc_custom_identities) / sizeof(rtc_custom_identities[0]); index++) {
    const t_rtc_custom_identity *identity = &rtc_custom_identities[index];
    if (memcmp(hash, identity->hash, 32))
      continue;
    unsigned count = 3 + 2 * identity->count;
    unsigned start = patch->wcnt_ops + patch->save_ops + patch->irqh_ops;
    if (count > MAX_PATCH_OPS - start)
      return false;
    patch->op[start++] = RTC_CUSTOM_MARKER | patch->hole_addr;
    patch->op[start++] = identity->ready;
    patch->op[start++] = identity->status;
    for (unsigned function = 0; function < identity->count; function++) {
      patch->op[start++] = 0x7E000000 | identity->functions[function].offset;
      patch->op[start++] = identity->functions[function].kind;
    }
    patch->rtc_ops = count;
    patch->hole_addr += RTC_CUSTOM_SLOT_SIZE;
    patch->hole_size -= RTC_CUSTOM_SLOT_SIZE;
    return true;
  }
  return false;
}

bool rtc_custom_supported(const t_patch *patch) {
  unsigned start = patch->wcnt_ops + patch->save_ops + patch->irqh_ops;
  if (start + patch->rtc_ops > MAX_PATCH_OPS || patch->rtc_ops < 5 || !(patch->rtc_ops & 1) ||
      (patch->op[start] & 0xFE000000) != RTC_CUSTOM_MARKER ||
      (patch->op[start] & 0x1FFFFFF) > MAX_GBA_ROM_SIZE - RTC_CUSTOM_SLOT_SIZE)
    return false;
  bool date = false;
  for (unsigned index = 3; index < patch->rtc_ops; index += 2) {
    uint32_t operation = patch->op[start + index], argument = patch->op[start + index + 1], kind = argument & 255;
    unsigned offset = operation & 0x1FFFFFF;
    if ((operation & 0xFE000000) != 0x7E000000 || kind > 12 || (offset & 1) ||
        offset < 192 || offset > MAX_GBA_ROM_SIZE - (kind == 11 ? 44 + (offset & 2) : kind == 10 ? 24 : kind == 12 ? 14 : 12) ||
        (kind == 10 ? argument >> 8 > 0x7FF0 : kind == 12 ?
         (argument & 0xFF800100U) != 0 : argument != kind))
      return false;
    if ((kind == 8 || kind == 9) && (patch->op[start + 1] >> 24 != 3 || patch->op[start + 2] >> 24 != 3))
      return false;
    date |= kind <= 1 || kind == 10 || kind == 11;
  }
  return date;
}

static __attribute__((noinline)) void put16(uint8_t *buffer, unsigned size, unsigned base, unsigned offset, uint16_t value) {
  if (offset < base || offset - base + 2 > size)
    return;
  volatile uint16_t *destination = (volatile uint16_t*)(buffer + offset - base);
  *destination = value;
}

static __attribute__((noinline)) void put32(uint8_t *buffer, unsigned size, unsigned base, unsigned offset, uint32_t value) {
  put16(buffer, size, base, offset, value);
  put16(buffer, size, base, offset + 2, value >> 16);
}

void rtc_custom_apply(uint8_t *buffer, unsigned size, unsigned base, const t_patch *patch) {
  const uint32_t *operations = patch->op + patch->wcnt_ops + patch->save_ops + patch->irqh_ops;
  unsigned slot = operations[0] & 0x1FFFFFF;
  // Reuse the firmware's existing reader/setter/reset templates instead of
  // storing second copies inside the wrapper blob. Virtual offsets are fixed
  // by rtc_custom.S and remain within the reserved 1 KiB slot.
  payload_apply_rom(buffer,size,base,(const uint8_t*)patch_rtc_gettimedate,176,slot+512);
  payload_apply_rom(buffer,size,base,(const uint8_t*)patch_rtc_settimedate,156,slot+688);
  payload_apply_rom(buffer,size,base,(const uint8_t*)patch_rtc_reset,28,slot+844);
  payload_apply_rom(buffer,size,base,rtc_custom_payload,rtc_custom_payload_size,slot);
  unsigned relocations = word(rtc_custom_payload, 10 * 4), table = word(rtc_custom_payload, 11 * 4);
  for (unsigned index = 0; index < relocations; index++) {
    unsigned offset = word(rtc_custom_payload, table + index * 8);
    put32(buffer, size, base, slot + offset, GBA_ROM_BASE + slot + word(rtc_custom_payload, table+index*8+4));
  }
  put32(buffer, size, base, slot + word(rtc_custom_payload, 12 * 4), operations[1]);
  put32(buffer, size, base, slot + word(rtc_custom_payload, 13 * 4), operations[2]);
  for (unsigned index = 3; index < patch->rtc_ops; index += 2) {
    unsigned target = operations[index] & 0x1FFFFFF;
    unsigned argument = operations[index + 1], kind = argument & 255;
    if (kind == 10) {
      // The enclosing hook has already pushed r0-r7/lr. Fill its global
      // decimal date, then continue its original day/night processing/pop.
      static const uint16_t code[] = {0x4802,0x4B03,0x4A03,0x4696,0x4718,0x46C0};
      payload_apply_rom(buffer,size,base,(const uint8_t*)code,sizeof(code),target);
      put32(buffer,size,base,target+12,0x03000000 | (argument >> 8));
      put32(buffer,size,base,target+16,GBA_ROM_BASE+slot+word(rtc_custom_payload,15*4)+1);
      put32(buffer,size,base,target+20,GBA_ROM_BASE+target+0x34+1);
      continue;
    }
    if (kind == 11) {
      // This reader stores year/month/day in a strided BCD buffer before
      // converting it and applying the game's own calendar/day-night logic.
      // Replace only GPIO acquisition; retain all postprocessing at +74.
      static const uint16_t code[] = {0xB570,0x1C06,0x3602,0x1C30,0x7834,0x4B06,0x4A06,0x4696,
          0x4718,0x7830,0x1C31,0x3902,0x7008,0x2000,0x7048,0x7034,0xE013,0x46C0};
      payload_apply_rom(buffer,size,base,(const uint8_t*)code,sizeof(code),target);
      unsigned padding = target & 2;
      if (padding) {
        put16(buffer,size,base,target+12,0x4A07);
        put16(buffer,size,base,target+36,0x46C0);
      }
      put32(buffer,size,base,target+36+padding,GBA_ROM_BASE+slot+513);
      put32(buffer,size,base,target+40+padding,GBA_ROM_BASE+target+19);
      continue;
    }
    if (kind == 12) {
      // A no-argument status reader stores a halfword in its own global and
      // returns that status. The ordinary SiiRtcInfo buffer ABI does not apply.
      unsigned padding = target & 2;
      put32(buffer,size,base,target,0x20404B01+padding/2);
      put32(buffer,size,base,target+4,0x47708018);
      put32(buffer,size,base,target+8+padding,0x03000000 | (argument >> 8));
      continue;
    }
    unsigned padding = target & 2 ? 2 : 0;
    put16(buffer, size, base, target, padding ? 0x4B01 : 0x4B00);
    put16(buffer, size, base, target + 2, 0x4718);
    if (padding)
      put16(buffer, size, base, target + 4, 0x46C0);
    put32(buffer, size, base, target + 4 + padding,
          GBA_ROM_BASE + slot + word(rtc_custom_payload, kind * 4) + 1);
  }
}
