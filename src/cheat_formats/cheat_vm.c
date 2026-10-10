/* SuperFW position-independent cheat executor. SPDX-License-Identifier: GPL-3.0-or-later
 * Operations follow mGBA's mCheat model; no parser/decryption runs in IRQ.
 * This section is copied with the cheat data, not into firmware EWRAM. */
#include "cheat_ops.h"
#include <stdbool.h>
#if defined(CHEAT_VM_HOST)
extern uint32_t cheat_vm_read(uint32_t, unsigned);
extern void cheat_vm_write(uint32_t, unsigned, uint32_t);
#else
static inline __attribute__((always_inline)) uint32_t cheat_vm_read(uint32_t a, unsigned w) {
  if (w == 1)
    return *(volatile uint8_t *)(uintptr_t)a;
  if (w == 2)
    return *(volatile uint16_t *)(uintptr_t)a;
  return *(volatile uint32_t *)(uintptr_t)a;
}
static inline __attribute__((always_inline)) void cheat_vm_write(uint32_t a, unsigned w,
                                                                 uint32_t v) {
  if (w == 1)
    *(volatile uint8_t *)(uintptr_t)a = v;
  else if (w == 2)
    *(volatile uint16_t *)(uintptr_t)a = v;
  else
    *(volatile uint32_t *)(uintptr_t)a = v;
}
#endif
void cheat_vm(const uint32_t *block) __attribute__((section(".vm")));
void cheat_vm(const uint32_t *block) {
  unsigned n = block[1], elseLoc = 0, endLoc = 0;
  const struct mCheat *ops = (const struct mCheat *)(block + 3);
  for (unsigned i = 0; i < n; i++) {
    const struct mCheat *c = &ops[i];
    uint32_t address = c->address, operand = c->operand;
    bool condition = true;
    unsigned yes = 0, no = 0;
    if (c->type >= CHEAT_IF_EQ) {
      uint32_t v = c->type >= CHEAT_IF_BUTTON ? 0 : cheat_vm_read(address, c->width);
      switch (c->type) {
      case CHEAT_IF_EQ:
        condition = v == operand;
        break;
      case CHEAT_IF_NE:
        condition = v != operand;
        break;
      case CHEAT_IF_LT:
        condition = (int32_t)v < (int32_t)operand;
        break;
      case CHEAT_IF_GT:
        condition = (int32_t)v > (int32_t)operand;
        break;
      case CHEAT_IF_LE:
        condition = (int32_t)v <= (int32_t)operand;
        break;
      case CHEAT_IF_GE:
        condition = (int32_t)v >= (int32_t)operand;
        break;
      case CHEAT_IF_ULT:
        condition = v < operand;
        break;
      case CHEAT_IF_UGT:
        condition = v > operand;
        break;
      case CHEAT_IF_AND:
        condition = (v & operand) != 0;
        break;
      case CHEAT_IF_LAND:
        condition = v && operand;
        break;
      case CHEAT_IF_NAND:
        condition = !(v & operand);
        break;
      case CHEAT_IF_BUTTON:
        // Select is the separate device activation button, not a replacement
        // for CodeBreaker KEYINPUT conditions. The menu hotkey stays unchanged.
        condition = !(cheat_vm_read(0x04000130, 2) & 4);
        break;
      default:
        condition = false;
        break;
      }
      yes = c->repeat;
      no = c->negativeRepeat;
    } else
      for (unsigned k = 0; k < c->repeat; k++) {
        uint32_t v = operand, target = address;
        if (c->type == CHEAT_ASSIGN_INDIRECT)
          target = cheat_vm_read(address, 4) + c->addressOffset;
        unsigned region = target >> 24;
        // An indirect pointer may legitimately be null during game transitions.
        // Never turn that into a write to BIOS/ROM or an unaligned word store.
        if (region < 2 || region > 7 || (target & (c->width - 1)))
          break;
        switch (c->type) {
        case CHEAT_AND:
          v = cheat_vm_read(target, c->width) & operand;
          break;
        case CHEAT_OR:
          v = cheat_vm_read(target, c->width) | operand;
          break;
        case CHEAT_ADD:
          v = cheat_vm_read(target, c->width) + operand;
          break;
        case CHEAT_SUB:
          v = cheat_vm_read(target, c->width) - operand;
          break;
        }
        cheat_vm_write(target, c->width, v);
        address += c->addressOffset;
        operand += c->operandOffset;
      }
    if (elseLoc && i == elseLoc) {
      i = endLoc;
      endLoc = 0;
    }
    if (yes && !condition)
      i += yes;
    else if (no) {
      elseLoc = i + yes;
      endLoc = elseLoc + no;
    }
  }
}
