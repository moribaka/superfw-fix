#include "webpatch.h"
#include "common.h"
#include "util.h"
#include "supercard_driver.h"

bool webpatch_open(t_webpatch_reader *reader, const char *romfn, const t_patch *patch) {
  reader->active = false;
  reader->pending = false;
  reader->remaining = 0;
  if (!patch || !patch->web_edits)
    return true;
  char filename[MAX_FN_LEN];
  if (patch->web_edits == 2) {
    strcpy(filename, PATCHDB_PATH);
    strcat(filename, file_basename(romfn));
  } else
    strcpy(filename, romfn);
  replace_extension(filename, ".patch");
  if (f_open(&reader->file, filename, FA_READ) != FR_OK)
    return false;
  reader->active = true;
  UINT read;
  if (f_lseek(&reader->file, 800) != FR_OK ||
      f_read(&reader->file, &reader->remaining, 4, &read) != FR_OK || read != 4) {
    webpatch_close(reader);
    return false;
  }
  return true;
}

bool webpatch_process(t_webpatch_reader *reader, uint8_t *buffer, unsigned size, unsigned base) {
  while (reader->remaining || reader->pending) {
    if (!reader->pending) {
      uint8_t record[6];
      UINT read;
      if (f_read(&reader->file, record, sizeof(record), &read) != FR_OK || read != sizeof(record))
        return false;
      memcpy(&reader->offset, record, 4);
      memcpy(&reader->value, record + 4, 2);
      reader->remaining--;
      reader->pending = true;
    }
    if (reader->offset >= base + size)
      return true;
    if (reader->offset >= base) {
      unsigned offset = reader->offset - base;
#ifdef __GBA__
      set_supercard_mode(MAPPED_SDRAM, true, false);
#endif
      volatile uint16_t *destination = (volatile uint16_t*)(buffer + offset);
      *destination = reader->value;
#ifdef __GBA__
      set_supercard_mode(MAPPED_SDRAM, true, true);
#endif
    }
    reader->pending = false;
  }
  return true;
}

void webpatch_close(t_webpatch_reader *reader) {
  if (reader->active)
    f_close(&reader->file);
  reader->active = false;
}
