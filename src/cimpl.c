/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

// Minimal C implementations for compactness. These functions should not
// be in the critical path of any useful stuff.

#include <stddef.h>
#include <stdint.h>

#include "compiler.h"

#ifndef BUILTIN_PREFIX
#define BUILTIN_PREFIX
#endif

#define CONCAT2(a, b) a ## b
#define CONCAT(a, b) CONCAT2(a, b)
#define FNAME(x) CONCAT(BUILTIN_PREFIX, x)

NOINLINE
int FNAME(strcmp)(const char *a, const char *b) {
  while (*a && (*a == *b)) {
    a++;
    b++;
  }
  return *a - *b;
}

NOINLINE
int FNAME(strncmp)(const char *a, const char *b, unsigned n) {
  while (n && *a && (*a == *b)) {
    n--; a++, b++;
  }
  if (!n)
    return 0;
  return *a - *b;
}

NOINLINE
char *FNAME(strchr)(const char *s, int c) {
  do {
    if (*s == (char)c)
      return (char *)s;
  } while (*s++);
  return 0;
}

NOINLINE
char *FNAME(strrchr)(const char *s, int c) {
  const char *last = 0;
  do {
    if (*s == (char)c)
      last = s;
  } while (*s++);
  return (char *)last;
}

NOINLINE
char *FNAME(strcat)(char *dest, const char *src) {
  char *p = dest;
  while (*p)
    p++;
  while ((*p++ = *src++))
    {}

  return dest;
}

NOINLINE
size_t FNAME(strlen)(const char *s) {
  size_t ret = 0;
  while (*s++)
    ret++;
  return ret;
}

NOINLINE EXTERNAL
void * FNAME(memmove)(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;

  if (d < s)
    while (n--)
      *d++ = *s++;
  else {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }

  return dst;
}

NOINLINE EXTERNAL
int FNAME(memcmp)(const void *a, const void *b, size_t n) {
  const unsigned char *p = a, *q = b;

  while (n--) {
    if (*p != *q)
      return (int)*p - (int)*q;
    p++;
    q++;
  }

  return 0;
}

NOINLINE EXTERNAL
void * FNAME(memset)(void *s, int c, size_t n) {
  unsigned char *p = s;
  unsigned char b = (unsigned char)c;
  uint32_t w = b * 0x01010101u;

  while (n && ((uintptr_t)p & 3)) {
    *p++ = b;
    n--;
  }

  while (n >= 4) {
    *(uint32_t *)p = w;
    p += 4;
    n -= 4;
  }

  while (n--)
    *p++ = b;

  return s;
}

NOINLINE EXTERNAL
char *FNAME(strcpy)(char *dst, const char *src) {
  char *d = dst;
  while ((*d++ = *src++));
  return dst;
}

