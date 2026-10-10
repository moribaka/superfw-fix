/* Halfword-safe freestanding helpers for the SDRAM import overlay. */
#include <stddef.h>
#include <stdint.h>
void *memcpy(void *to, const void *from, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint16_t *p = (uint16_t *)((uintptr_t)to + (i & ~(size_t)1));
    unsigned s = (i & 1) * 8;
    *p = (*p & ~(255u << s)) | ((uint16_t)((const uint8_t *)from)[i] << s);
  }
  return to;
}
void *memset(void *to, int value, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint16_t *p = (uint16_t *)((uintptr_t)to + (i & ~(size_t)1));
    unsigned s = (i & 1) * 8;
    *p = (*p & ~(255u << s)) | ((uint16_t)(value & 255) << s);
  }
  return to;
}
