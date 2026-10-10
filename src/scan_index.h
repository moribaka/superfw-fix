#ifndef SCAN_INDEX_H
#define SCAN_INDEX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  SCAN_DMA = 1, SCAN_IRQ = 2, SCAN_NEST = 4, SCAN_ENTRY = 8,
  SCAN_WAIT = 16, SCAN_REFERENCE = 32
};
typedef struct { uint32_t offset, kinds; } t_scan_candidate;
typedef struct { uint32_t pool, source; } t_scan_reference;
typedef struct {
  t_scan_candidate *items;
  t_scan_reference *references;
  unsigned count, reference_count;
  uint32_t *pending_irq;
  unsigned pending_irq_count;
} t_scan_index;

// With an index, iterate candidate words only. Halfword consumers examine
// both halves, in the same order and with the same end bounds as before.
static __attribute__((noinline, optimize("Os"))) bool scan_index_next(
    const t_scan_index *index, unsigned kinds, unsigned *cursor,
    unsigned step, unsigned limit, unsigned *offset) {
  if (!index) {
    if (*cursor > limit) return false;
    *offset = *cursor;
    *cursor += step;
    return true;
  }
  unsigned halves = step == 2 ? 2 : 1;
  while (*cursor / halves < index->count) {
    unsigned record = *cursor / halves, half = *cursor % halves;
    if (!(index->items[record].kinds & kinds)) {
      *cursor = (record + 1) * halves;
      continue;
    }
    *offset = index->items[record].offset + half * 2;
    ++*cursor;
    if (*offset > limit) return false;
    return true;
  }
  return false;
}
#endif
