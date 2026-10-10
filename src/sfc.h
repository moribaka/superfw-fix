#ifndef SUPERFW_SFC_H
#define SUPERFW_SFC_H
#include <stdint.h>
#include <stdbool.h>
#include "fatfs/ff.h"

typedef struct {
  uint32_t size, padded_size, source_skip, rom_offset, sram_mask;
  bool lorom;
  uint8_t header[64];
} t_sfc_info;

bool sfc_inspect(FIL *file, unsigned file_size, t_sfc_info *info);
bool sfc_prepare(uint8_t *core, unsigned core_size, t_sfc_info *info);
bool sfc_finish(uint8_t *core, const t_sfc_info *info);
#endif
