
#include <stdint.h>
#include <stdarg.h>
#include "common.h"
#include "gbahw.h"
#include "nanoprintf.h"
#include "fatfs/ff.h"

typedef struct { char *dst; size_t cap; size_t cur; } log_buf_t;

static void log_putc(int c, void *ctx) {
  log_buf_t *b = (log_buf_t *)ctx;
  if (b->cur < b->cap) b->dst[b->cur++] = (char)c;
}

void write_log(const char *fname, int line, const char *format, ...) {
  char buffer[256];
  log_buf_t buf = { buffer, sizeof(buffer) - 1, 0 };  /* -1 reserves the '\n' */

  npf_pprintf(log_putc, &buf, "[%s:%d] ", fname, line);

  va_list args;
  va_start(args, format);
  npf_vpprintf(log_putc, &buf, format, args);
  va_end(args);

  buffer[buf.cur++] = '\n';

  FIL fil;
  UINT written;
  if (f_open(&fil, "/superfwlog.txt", FA_OPEN_APPEND | FA_WRITE) != FR_OK)
    return;
  f_write(&fil, buffer, buf.cur, &written);
  f_close(&fil);
}

void write_log_emu(const char *fname, int line, const char *format, ...) {
  char buffer[256];
  log_buf_t buf = { buffer, sizeof(buffer) - 2, 0 };

  npf_pprintf(log_putc, &buf, "[%s:%d] ", fname, line);

  va_list args;
  va_start(args, format);
  npf_vpprintf(log_putc, &buf, format, args);
  va_end(args);

  buffer[buf.cur++] = 0;

  // This is the Write Debug string for gpsp (supercard/chis branch)
  // Ensure GCC doesnt optimize it all away the little fucker
  asm volatile("str %0, [%1]" :: "r"(buffer), "r"(0x04000324) : "memory");
}

static void uart_putc(int c, void *ctx) {
  while (REG_SIOCNT & (1<<4));     // Wait while send buffer is full
  REG_SIODATA8 = (unsigned char)c;
}

void write_log_uart(const char *fname, int line, const char *format, ...) {

  npf_pprintf(uart_putc, NULL, "[%s:%d] ", fname, line);

  va_list args;
  va_start(args, format);
  npf_vpprintf(uart_putc, NULL, format, args);
  va_end(args);

  uart_putc('\r', NULL);
  uart_putc('\n', NULL);
}


