/* Unified import/runtime operations. mCheat layout and enums derive from mGBA.
 * Copyright (c) 2013-2016 Jeffrey Pfau. SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
enum mCheatType {
  CHEAT_ASSIGN,
  CHEAT_ASSIGN_INDIRECT,
  CHEAT_AND,
  CHEAT_ADD,
  CHEAT_SUB,
  CHEAT_OR,
  CHEAT_IF_EQ,
  CHEAT_IF_NE,
  CHEAT_IF_LT,
  CHEAT_IF_GT,
  CHEAT_IF_LE,
  CHEAT_IF_GE,
  CHEAT_IF_ULT,
  CHEAT_IF_UGT,
  CHEAT_IF_AND,
  CHEAT_IF_LAND,
  CHEAT_IF_NAND,
  CHEAT_IF_BUTTON,
  CHEAT_NEVER
};
struct mCheat {
  uint32_t type, width, address, operand, repeat, negativeRepeat;
  int32_t addressOffset, operandOffset;
};
struct mCheatPatch {
  uint32_t address;
  int segment;
  uint32_t value;
  int width;
  uint32_t applied, checkValue, check;
};
_Static_assert(sizeof(struct mCheat) == 32, "Cheat VM operation ABI");
