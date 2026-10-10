#ifndef RTC_STATE_H
#define RTC_STATE_H

#include <stdbool.h>
#include <stdint.h>

void rtc_state_path(const char *rom, char *path);
bool rtc_state_load(const char *path, uint32_t *timestamp, uint32_t *speed);
bool rtc_state_save(const char *path, uint32_t timestamp, uint32_t speed);
bool rtc_state_prepare_pending(const char *path);
bool rtc_state_flush_pending(uint32_t timestamp, uint32_t speed, bool valid);

#define RTC_RUNTIME_SIGNATURE 0xD00000DF

#endif
