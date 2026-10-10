#ifndef RTC_SPEED_H
#define RTC_SPEED_H

#include <stdint.h>

#define RTC_SPEED_CNT 6
#define RTC_SPEED_DEFAULT 3

static inline uint32_t rtc_speed_pack(unsigned speed) {
  static const uint16_t steps[RTC_SPEED_CNT] = {0, 1, 8, 64, 768, 6144};
  if (speed >= RTC_SPEED_CNT)
    speed = 0;
  return (speed << 16) | steps[speed];
}

static inline unsigned rtc_speed_unpack(uint32_t state) {
  unsigned speed = (state >> 16) & 255;
  return speed < RTC_SPEED_CNT ? speed : 0;
}

#endif
