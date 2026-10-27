#ifndef WEBPATCH_H
#define WEBPATCH_H

#include "patchengine.h"
#include "fatfs/ff.h"

typedef struct {
  FIL file;
  uint32_t remaining;
  uint32_t offset;
  uint16_t value;
  bool active;
  bool pending;
} t_webpatch_reader;

bool webpatch_open(t_webpatch_reader *reader, const char *romfn, const t_patch *patch);
bool webpatch_process(t_webpatch_reader *reader, uint8_t *buffer, unsigned size, unsigned base);
void webpatch_close(t_webpatch_reader *reader);

#endif
