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

#ifndef SUPERFW_CHEATS_H
#define SUPERFW_CHEATS_H
#include <stdint.h>

typedef struct {
  uint16_t slen;       // String length including padding (bytes)
  uint16_t codelen;    // Data payload length (bytes)
  uint16_t enabled;    // SDRAM/NOR mapping requires halfword writes
  uint16_t _pad;
  uint8_t data[];
} t_cheathdr;

typedef struct {
  t_cheathdr h;
  char title[256];
} t_cheathdr_ext;

typedef struct {
  uint8_t opcode;            // The codebreaker opcode (0 to 15)
  uint8_t blen;              // Number of bytes used by this cheat (usually 8, this struct)
  uint16_t value;            // Cheat value
  uint32_t address;          // Cheat address
} t_cheat_predec;

int open_read_cheats(uint8_t *buffer, unsigned buffersize, const char *fn);

#define CHEAT_VM_MAGIC 0x325448FEu
#define CHEAT_ENABLED_FLAG 0x80000000u
#define CHEAT_COUNT_MASK 0x7FFFFFFFu
enum { CHEAT_INPUT_MAX = 512 * 1024, CHEAT_WORK_SIZE = 512 * 1024 };
typedef struct {
  const char *input;
  unsigned input_size;
  uint8_t *output;
  unsigned output_size;
  void *work;
  unsigned work_size;
  unsigned error_line;
  unsigned error;
} t_cheat_import;
enum {
  CheatOK, CheatSyntax, CheatCapacity, CheatIncomplete, CheatUnsupported,
  CheatHook, CheatROMPatch, CheatAmbiguous, CheatRange, CheatFile
};
int cheat_compile(t_cheat_import *request);
extern unsigned cheat_error_line, cheat_error_code;
#endif

