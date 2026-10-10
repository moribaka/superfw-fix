#include "rtc_state.h"
#include "rtc_speed.h"
#include "config.h"
#include "fatfs/ff.h"
#include "util.h"
#include <string.h>

#pragma GCC optimize ("Os")

#define RTC_PENDING_PATH SUPERFW_DIR "/rtc-pending.txt"

void rtc_state_path(const char *rom, char *path) {
  if (strlen(ROMCONFIG_PATH) + strlen(file_basename(rom)) >= MAX_FN_LEN) {
    path[0] = 0;
    return;
  }
  strcpy(path, ROMCONFIG_PATH);
  strcat(path, file_basename(rom));
  replace_extension(path, ".rtc");
}

bool rtc_state_load(const char *path, uint32_t *timestamp, uint32_t *speed) {
  FIL file;
  if (!path[0] || f_open(&file, path, FA_READ) != FR_OK)
    return false;
  uint32_t record[4];
  UINT count;
  bool valid = f_read(&file, record, sizeof(record), &count) == FR_OK && count == sizeof(record) &&
               f_size(&file) == sizeof(record) && record[0] == 0x31435452 && record[2] < RTC_SPEED_CNT;
  f_close(&file);
  if (valid) {
    *timestamp = record[1];
    *speed = record[2];
  }
  return valid;
}

bool rtc_state_save(const char *path, uint32_t timestamp, uint32_t speed) {
  if (!path[0] || speed >= RTC_SPEED_CNT)
    return false;
  uint32_t previous_timestamp, previous_speed;
  if (rtc_state_load(path, &previous_timestamp, &previous_speed) &&
      timestamp == previous_timestamp && speed == previous_speed)
    return true;
  f_mkdir(SUPERFW_DIR);
  f_mkdir(ROMCONFIG_PATH);
  FIL file;
  if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    return false;
  uint32_t record[] = {0x31435452, timestamp, speed, 0x31435452 ^ timestamp ^ speed};
  UINT count;
  bool success = f_write(&file, record, sizeof(record), &count) == FR_OK && count == sizeof(record);
  return f_close(&file) == FR_OK && success;
}

bool rtc_state_prepare_pending(const char *path) {
  if (!path || !path[0]) {
    FRESULT result = f_unlink(RTC_PENDING_PATH);
    return result == FR_OK || result == FR_NO_FILE;
  }
  f_mkdir(SUPERFW_DIR);
  FIL file;
  if (f_open(&file, RTC_PENDING_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    return false;
  unsigned length = strlen(path) + 1;
  UINT count;
  bool success = f_write(&file, path, length, &count) == FR_OK && count == length;
  return f_close(&file) == FR_OK && success;
}

bool rtc_state_flush_pending(uint32_t timestamp, uint32_t speed, bool valid) {
  FIL file;
  if (f_open(&file, RTC_PENDING_PATH, FA_READ) != FR_OK)
    return true;
  char path[MAX_FN_LEN];
  UINT count;
  bool loaded = f_read(&file, path, sizeof(path), &count) == FR_OK && count && path[count - 1] == 0;
  f_close(&file);
  if (valid && loaded && !rtc_state_save(path, timestamp, speed))
    return false;
  f_unlink(RTC_PENDING_PATH);
  return true;
}
