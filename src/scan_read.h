#ifndef SUPERFW_SCAN_READ_H
#define SUPERFW_SCAN_READ_H
#include <stdint.h>

typedef uint16_t scan_alias16 __attribute__((may_alias));
typedef uint32_t scan_alias32 __attribute__((may_alias));

// Most scan accesses are aligned instructions in one buffer. Avoid assembling
// every instruction with four byte loads, while preserving unaligned and
// prefix-boundary behavior for Thumb instructions and cross-chunk matches.
static inline __attribute__((always_inline)) uint32_t scan_read(
    const uint8_t *data, const uint8_t *prefix, unsigned prefix_size,
    unsigned offset, unsigned width) {
  const uint8_t *ptr;
  if (offset >= prefix_size)
    ptr = data + offset - prefix_size;
  else if (width <= prefix_size - offset)
    ptr = prefix + offset;
  else {
    uint32_t value = 0;
    for (unsigned byte = 0; byte < width; byte++) {
      unsigned position = offset + byte;
      value |= (uint32_t)(position < prefix_size ? prefix[position] : data[position - prefix_size]) << (8 * byte);
    }
    return value;
  }
  if (width == 4 && !((uintptr_t)ptr & 3))
    return *(const scan_alias32*)ptr;
  if (width == 2 && !((uintptr_t)ptr & 1))
    return *(const scan_alias16*)ptr;
  if (width == 4 && !((uintptr_t)ptr & 1))
    return *(const scan_alias16*)ptr | (uint32_t)*(const scan_alias16*)(ptr + 2) << 16;
  uint32_t value = 0;
  for (unsigned byte = 0; byte < width; byte++)
    value |= (uint32_t)ptr[byte] << (8 * byte);
  return value;
}
#endif
