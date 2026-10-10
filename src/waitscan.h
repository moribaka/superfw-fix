#ifndef WAITSCAN_H
#define WAITSCAN_H

#include <stdbool.h>
#include <stdint.h>
#include "scan_index.h"

#define WAITSCAN_CONTEXT_SIZE 8192
#define WAITSCAN_REJECT_OPERATION 0x80000000U

bool waitscan_process(const uint8_t *data, unsigned size, uint32_t base,
                      const uint8_t *prefix, unsigned prefix_size,
                      bool (*emit)(void *, uint32_t), void *context,
                      void (*progress)(unsigned));
bool waitscan_process_progress(const uint8_t *data, unsigned size, uint32_t base,
                      const uint8_t *prefix, unsigned prefix_size,
                      bool (*emit)(void *, uint32_t), void *context,
                      void (*progress)(unsigned), unsigned progress_base, unsigned progress_divisor);
bool waitscan_process_progress_indexed(const uint8_t *data, unsigned size, uint32_t base,
                      const uint8_t *prefix, unsigned prefix_size,
                      bool (*emit)(void *, uint32_t), void *context,
                      void (*progress)(unsigned), unsigned progress_base, unsigned progress_divisor, t_scan_index *index);

#endif
