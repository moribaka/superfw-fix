#include "waitscan.h"
#include "scan_read.h"
#include "scan_index.h"

#pragma GCC optimize ("Oz")

typedef struct {
  const uint8_t *prefix;
  const uint8_t *data;
  unsigned prefix_size;
  unsigned size;
  uint32_t base;
  bool (*emit)(void *, uint32_t);
  void *context;
} t_scan_view;

typedef struct {
  uint32_t value[16];
  unsigned known;
} t_registers;

static inline __attribute__((always_inline)) uint32_t read_value(const t_scan_view *view, unsigned offset, unsigned width) {
  return scan_read(view->data, view->prefix, view->prefix_size, offset, width);
}

static void assign(t_registers *regs, unsigned destination, bool known, uint32_t value) {
  regs->value[destination] = value;
  regs->known = (regs->known & ~(1U << destination)) | ((unsigned)known << destination);
}

static bool known_register(const t_registers *regs, unsigned reg) {
  return (regs->known >> reg) & 1;
}

static uint32_t rotate_right(uint32_t value, unsigned shift) {
  shift &= 31;
  return shift ? (value >> shift) | (value << (32 - shift)) : value;
}

static bool shifted_value(uint32_t value, unsigned kind, unsigned shift,
                          bool immediate, uint32_t *result) {
  if (!shift && !immediate) {
    *result = value;
    return true;
  }
  if (!shift && kind && immediate) {
    if (kind == 3)
      return false;
    shift = 32;
  }
  if (kind == 0)
    *result = shift < 32 ? value << shift : 0;
  else if (kind == 1)
    *result = shift < 32 ? value >> shift : 0;
  else if (kind == 2)
    *result = (uint32_t)((int32_t)value >> (shift < 32 ? shift : 31));
  else
    *result = rotate_right(value, shift);
  return true;
}

static bool io_address(uint32_t value) {
  return value >= 0x04000000 && value < 0x04000400;
}

static unsigned thumb_write_mask(unsigned instruction) {
  unsigned destination = instruction & 7;
  if (((instruction & 0xF800) == 0 && !(instruction & 0x7C0) &&
       destination == ((instruction >> 3) & 7)) ||
      ((instruction & 0xF800) == 0x1800 && (instruction & 0x400) &&
       !(instruction & 0x1C0) && destination == ((instruction >> 3) & 7)) ||
      ((instruction & 0xF000) == 0x3000 && !(instruction & 255)))
    return 0;
  if ((instruction & 0xE000) == 0)
    return 1U << destination;
  if ((instruction & 0xE000) == 0x2000)
    return ((instruction >> 11) & 3) == 1 ? 0 : 1U << ((instruction >> 8) & 7);
  if ((instruction & 0xFC00) == 0x4000) {
    unsigned kind = (instruction >> 6) & 15;
    return kind == 8 || kind == 10 || kind == 11 ? 0 : 1U << destination;
  }
  if ((instruction & 0xFC00) == 0x4400) {
    unsigned kind = (instruction >> 8) & 3;
    if (kind == 2 && (destination | ((instruction >> 4) & 8)) == ((instruction >> 3) & 15))
      return 0;
    return kind == 1 ? 0 : kind == 3 ? 0xFFFF : 1U << (destination | ((instruction >> 4) & 8));
  }
  if ((instruction & 0xF800) == 0x4800 || (instruction & 0xF000) == 0xA000)
    return 1U << ((instruction >> 8) & 7);
  if ((instruction & 0xF000) == 0x5000)
    return ((instruction >> 9) & 7) > 2 ? 1U << destination : 0;
  if ((instruction & 0xE000) == 0x6000 || (instruction & 0xF000) == 0x8000)
    return instruction & 0x800 ? 1U << destination : 0;
  if ((instruction & 0xF000) == 0x9000)
    return instruction & 0x800 ? 1U << ((instruction >> 8) & 7) : 0;
  if ((instruction & 0xFE00) == 0xBC00)
    return (instruction & 255) | (1U << 13);
  if ((instruction & 0xFE00) == 0xB400 || (instruction & 0xFF00) == 0xB000)
    return 1U << 13;
  if ((instruction & 0xF000) == 0xC000)
    return (1U << ((instruction >> 8) & 7)) | (instruction & 0x800 ? instruction & 255 : 0);
  if (((instruction & 0xF000) == 0xD000 && ((instruction >> 8) & 15) < 14) ||
      (instruction & 0xF800) == 0xE000)
    return 0;
  return 0xFFFF;
}

static unsigned arm_write_mask(uint32_t instruction) {
  bool word_memory = (instruction & 0x0C000000) == 0x04000000;
  bool half_memory = (instruction & 0x0E0000F0) == 0x000000B0;
  if (word_memory || half_memory)
    return (instruction & 0x00100000 ? 1U << ((instruction >> 12) & 15) : 0) |
           (!(instruction & 0x01000000) || (instruction & 0x00200000) ? 1U << ((instruction >> 16) & 15) : 0);
  if ((instruction & 0x0C000000) == 0 && ((instruction & 0x02000000) || !(instruction & 0x90))) {
    unsigned kind = (instruction >> 21) & 15;
    unsigned destination = (instruction >> 12) & 15;
    if (destination != 15 && (instruction & 0x02000000) && !(instruction & 255) &&
        destination == ((instruction >> 16) & 15) &&
        (kind == 1 || kind == 2 || kind == 4 || kind == 12 || kind == 14))
      return 0;
    if (destination != 15 && kind == 13 && !(instruction & 0x02000FF0) && destination == (instruction & 15))
      return 0;
    return kind >= 8 && kind <= 11 ? 0 : 1U << ((instruction >> 12) & 15);
  }
  if ((instruction & 0x0E000000) == 0x08000000)
    return (instruction & 0x00100000 ? instruction & 0xFFFF : 0) |
           (instruction & 0x00200000 ? 1U << ((instruction >> 16) & 15) : 0);
  if ((instruction & 0x0F000000) == 0x0A000000)
    return 0;
  return 0xFFFF;
}

static bool constant_assignment(uint32_t instruction, bool thumb) {
  if (thumb)
    return (instruction & 0xF800) == 0x2000 || (instruction & 0xF800) == 0x4800;
  return (instruction & 0xFFFF0000) == 0xE59F0000 ||
         (instruction & 0xFFFF0000) == 0xE51F0000 ||
         (instruction & 0xFFFF0000) == 0xE3A00000;
}

static bool shared_store_loop(const t_scan_view *view, unsigned store, bool thumb) {
  unsigned width = thumb ? 2 : 4;
  uint32_t instruction = read_value(view, store, width);
  unsigned dependencies = 1U << (thumb ? (instruction >> 3) & 7 : (instruction >> 16) & 15);
  bool register_offset = thumb ? (instruction & 0xF000) == 0x5000 :
      (instruction & 0x0C000000) == 0x04000000 ? instruction & 0x02000000 : !(instruction & 0x00400000);
  if (register_offset)
    dependencies |= 1U << (thumb ? (instruction >> 6) & 7 : instruction & 15);
  for (unsigned offset = store + width, steps = 0; offset + width <= view->size && steps < 128; offset += width, steps++) {
    instruction = read_value(view, offset, width);
    int64_t target = -1;
    if (thumb) {
      if ((instruction & 0xF000) == 0xD000 && ((instruction >> 8) & 15) < 14)
        target = (int64_t)offset + 4 + (int8_t)instruction * 2;
      else if ((instruction & 0xF800) == 0xE000)
        target = (int64_t)offset + 4 + ((int32_t)(instruction << 21) >> 20);
      else if ((instruction & 0xFC00) == 0x4400 &&
               (((instruction >> 8) & 3) == 3 ||
                (((instruction >> 8) & 3) != 1 && (instruction & 0x87) == 0x87)))
        break;
      else if ((instruction & 0xF000) == 0xF000 || (instruction & 0xFF00) == 0xBD00 ||
               (instruction & 0xFF00) == 0xDF00)
        break;
    }
    else {
      if ((instruction & 0x0F000000) == 0x0A000000)
        target = (int64_t)offset + 8 + ((int32_t)(instruction << 8) >> 6);
      else if ((instruction & 0x0FFFFFF0) == 0x012FFF10 ||
               (instruction & 0x0F000000) == 0x0B000000 ||
               (instruction & 0x0F000000) == 0x0F000000 ||
               (arm_write_mask(instruction) & (1U << 15)))
        break;
    }
    if (target > (int64_t)offset ||
        (target == -1 && (thumb ? thumb_write_mask(instruction) : arm_write_mask(instruction)) == 0xFFFF))
      break;
    if (target == -1 || target > store || offset - target > 1024 || (target & (width - 1)))
      continue;
    if (target < 0)
      break;
    unsigned written = 0, restored = 0;
    for (unsigned current = target; current <= offset; current += width) {
      uint32_t body = read_value(view, current, width);
      unsigned mask = thumb ? thumb_write_mask(body) : arm_write_mask(body);
      if (mask == 0xFFFF || (mask & (1U << 15)) ||
          (current != offset && (thumb ?
            ((body & 0xF000) == 0xD000 || (body & 0xF800) == 0xE000) :
            (body & 0x0E000000) == 0x0A000000)))
        return false;
      if (current < store) {
        restored &= ~mask;
        if (constant_assignment(body, thumb))
          restored |= mask;
      }
      written |= mask;
    }
    if (written & dependencies & ~restored)
      return true;
    if (thumb ? (instruction & 0xF800) == 0xE000 : (instruction >> 28) == 14)
      break;
  }
  return false;
}

static bool store_wait(const t_scan_view *view, unsigned offset, uint32_t address,
                       unsigned width, bool thumb) {
  if (view->base + offset < 0xC0)
    return true;
  if (address != 0x04000204 && !(address == 0x04000205 && width == 1))
    return true;
  uint32_t operation = (thumb ? 0x10000000 : 0x20000000) | (view->base + offset);
  if (shared_store_loop(view, offset, thumb))
    operation |= WAITSCAN_REJECT_OPERATION;
  return view->emit(view->context, operation);
}

static bool thumb_leaf_clobbers(const t_scan_view *view, unsigned start, unsigned *clobbers) {
  unsigned written = 1U << 14;
  int stack_delta = 0;
  if (start + 4 > view->size)
    return false;
  unsigned first = read_value(view, start, 2);
  if ((first == 0xDF01 || first == 0xDF0B || first == 0xDF0C) &&
      read_value(view, start + 2, 2) == 0x4770) {
    *clobbers = 0x500F;
    return true;
  }
  for (unsigned offset = start, steps = 0; offset + 2 <= view->size && steps < 64; offset += 2, steps++) {
    unsigned instruction = read_value(view, offset, 2);
    unsigned destination = instruction & 7;
    bool writes = false;
    if ((instruction & 0xE000) == 0 && (instruction & 0xF800) != 0x1800)
      writes = true;
    else if ((instruction & 0xF800) == 0x1800)
      writes = true;
    else if ((instruction & 0xE000) == 0x2000) {
      destination = (instruction >> 8) & 7;
      writes = ((instruction >> 11) & 3) != 1;
    }
    else if ((instruction & 0xFC00) == 0x4000) {
      unsigned kind = (instruction >> 6) & 15;
      writes = kind != 8 && kind != 10 && kind != 11;
    }
    else if ((instruction & 0xFC00) == 0x4400) {
      unsigned kind = (instruction >> 8) & 3;
      if (kind == 3) {
        if (instruction != 0x4770 || stack_delta)
          return false;
        *clobbers = written;
        return true;
      }
      destination |= (instruction >> 4) & 8;
      writes = kind != 1;
      if (writes && destination >= 13)
        return false;
    }
    else if ((instruction & 0xF800) == 0x4800) {
      destination = (instruction >> 8) & 7;
      writes = true;
    }
    else if ((instruction & 0xF000) == 0x5000)
      writes = ((instruction >> 9) & 7) > 2;
    else if ((instruction & 0xE000) == 0x6000 || (instruction & 0xF000) == 0x8000)
      writes = instruction & 0x800;
    else if ((instruction & 0xF000) == 0x9000 || (instruction & 0xF000) == 0xA000) {
      destination = (instruction >> 8) & 7;
      writes = (instruction & 0xF000) == 0xA000 || (instruction & 0x800);
    }
    else if ((instruction & 0xFF00) == 0xB000) {
      int adjustment = (instruction & 127) * 4;
      stack_delta += instruction & 128 ? -adjustment : adjustment;
      if (stack_delta < -1024 || stack_delta > 1024)
        return false;
    }
    else
      return false;
    if (writes)
      written |= 1U << destination;
  }
  return false;
}

static bool scan_thumb_wait_wrapper(const t_scan_view *view, unsigned start) {
  static const uint16_t instructions[] = {
    0xB590, 0xB084, 0x466F, 0x4800, 0x6038, 0x4800, 0x6078, 0x4800,
    0x60B8, 0x1C38, 0x300C, 0x6879, 0x880A, 0x8002, 0x6878, 0x22C0,
    0x0152, 0x1C11, 0x8001, 0x68B8, 0x2120, 0x8001, 0x6838, 0x6839,
    0x683A, 0x88D3, 0x2400, 0x4023, 0x1C1C, 0x80D4, 0x888A, 0x2300,
    0x401A, 0x1C13, 0x808B, 0x8801, 0x2200, 0x4011, 0x1C0A, 0x8002,
    0x6838, 0x8841, 0x2200, 0x4011, 0x1C0A, 0x23FD, 0x1C11, 0x4319,
    0x1C0A, 0x8042, 0x68B8, 0x2100, 0x8001, 0x6878, 0x1C39, 0x310C,
    0x880A, 0x8002
  };
  if (view->size - start < sizeof(instructions))
    return true;
  for (unsigned index = 0; index < sizeof(instructions) / sizeof(instructions[0]); index++) {
    unsigned instruction = read_value(view, start + index * 2, 2);
    if (index == 3 || index == 5 || index == 7)
      instruction &= 0xFF00;
    if (instruction != instructions[index])
      return true;
  }
  for (unsigned index = 3; index <= 7; index += 2) {
    unsigned offset = start + index * 2;
    unsigned target = (offset & ~3U) + 4 + (read_value(view, offset, 2) & 255) * 4;
    if (target < start + sizeof(instructions) || target > view->size - 4)
      return true;
    uint32_t value = read_value(view, target, 4);
    if (index == 5 ? value != 0x04000204 : value < 0x08000000 || value >= 0x0E000000)
      return true;
  }
  return store_wait(view, start + 36, 0x04000204, 2, true) &&
         store_wait(view, start + 114, 0x04000204, 2, true);
}

static bool scan_thumb(const t_scan_view *view, unsigned start, unsigned pool,
                       unsigned initial_reg, uint32_t initial_value) {
  t_registers regs = {{0}, 0};
  assign(&regs, initial_reg, true, initial_value);
  unsigned join = view->size;
  unsigned origin = start;
  t_registers branch_regs = {{0}, 0};
  for (unsigned offset = start + 2, steps = 0;
       offset + 2 <= view->size && offset - origin <= 1024 && steps < 128; offset += 2, steps++) {
    if (offset >= join && join != view->size) {
      if (offset != join)
        break;
      regs.known &= branch_regs.known;
      for (unsigned reg = 0; reg < 16; reg++)
        if (regs.value[reg] != branch_regs.value[reg])
          regs.known &= ~(1U << reg);
      join = view->size;
    }
    if (offset == pool)
      break;
    unsigned instruction = read_value(view, offset, 2);
    unsigned destination = instruction & 7;
    unsigned source = (instruction >> 3) & 7;
    if ((instruction & 0xF800) < 0x1800) {
      unsigned shift = (instruction >> 6) & 31;
      unsigned kind = instruction >> 11;
      uint32_t value = regs.value[source];
      if (kind == 0)
        value <<= shift;
      else if (kind == 1)
        value = shift ? value >> shift : 0;
      else
        value = (uint32_t)((int32_t)value >> (shift ? shift : 31));
      assign(&regs, destination, known_register(&regs, source), value);
    }
    else if ((instruction & 0xF800) == 0x1800) {
      unsigned operand = (instruction >> 6) & 7;
      bool immediate = instruction & 0x400;
      bool known = known_register(&regs, source) && (immediate || known_register(&regs, operand));
      uint32_t value = immediate ? operand : regs.value[operand];
      value = instruction & 0x200 ? regs.value[source] - value : regs.value[source] + value;
      assign(&regs, destination, known, value);
    }
    else if ((instruction & 0xE000) == 0x2000) {
      unsigned kind = (instruction >> 11) & 3;
      destination = (instruction >> 8) & 7;
      unsigned immediate = instruction & 255;
      if (kind == 0)
        assign(&regs, destination, true, immediate);
      else if (kind != 1)
        assign(&regs, destination, known_register(&regs, destination),
               kind == 2 ? regs.value[destination] + immediate : regs.value[destination] - immediate);
    }
    else if ((instruction & 0xFC00) == 0x4000) {
      unsigned kind = (instruction >> 6) & 15;
      if (kind == 8 || kind == 10 || kind == 11)
        continue;
      bool known = known_register(&regs, destination) && known_register(&regs, source);
      uint32_t value = 0;
      if (kind == 0)
        value = regs.value[destination] & regs.value[source];
      else if (kind == 1)
        value = regs.value[destination] ^ regs.value[source];
      else if (kind >= 2 && kind <= 4)
        shifted_value(regs.value[destination], kind - 2, regs.value[source] & 255, false, &value);
      else if (kind == 7)
        shifted_value(regs.value[destination], 3, regs.value[source] & 255, false, &value);
      else if (kind == 9) {
        known = known_register(&regs, source);
        value = 0U - regs.value[source];
      }
      else if (kind == 12)
        value = regs.value[destination] | regs.value[source];
      else if (kind == 14)
        value = regs.value[destination] & ~regs.value[source];
      else if (kind == 15) {
        known = known_register(&regs, source);
        value = ~regs.value[source];
      }
      else
        known = false;
      assign(&regs, destination, known, value);
    }
    else if ((instruction & 0xFC00) == 0x4400) {
      destination |= (instruction >> 4) & 8;
      source = (instruction >> 3) & 15;
      unsigned kind = (instruction >> 8) & 3;
      if (kind == 3) {
        uint32_t address = regs.value[source];
        unsigned absolute = address & 0x1FFFFFE;
        if ((instruction & 0x87) || !known_register(&regs, source) || !(address & 1) ||
            address < 0x08000000 || address >= 0x0E000000 ||
            absolute < view->base || absolute - view->base + 2 > view->size || join != view->size)
          break;
        unsigned target = absolute - view->base;
        origin = target;
        offset = target - 2;
        continue;
      }
      if (destination == 15 || source == 15)
        break;
      if (kind == 0)
        assign(&regs, destination, known_register(&regs, destination) && known_register(&regs, source),
               regs.value[destination] + regs.value[source]);
      else if (kind == 2)
        assign(&regs, destination, known_register(&regs, source), regs.value[source]);
    }
    else if ((instruction & 0xF800) == 0x4800) {
      unsigned target = (offset & ~3U) + 4 + (instruction & 255) * 4;
      destination = (instruction >> 8) & 7;
      bool known = target <= view->size - 4;
      assign(&regs, destination, known, known ? read_value(view, target, 4) : 0);
    }
    else if ((instruction & 0xF000) == 0x5000) {
      unsigned operand = (instruction >> 6) & 7;
      unsigned kind = (instruction >> 9) & 7;
      if (kind > 2)
        assign(&regs, destination, false, 0);
      else if (known_register(&regs, source) && known_register(&regs, operand) &&
               !store_wait(view, offset, regs.value[source] + regs.value[operand],
                           kind == 0 ? 4 : kind == 1 ? 2 : 1, true))
        return false;
    }
    else if ((instruction & 0xE000) == 0x6000 || (instruction & 0xF000) == 0x8000) {
      unsigned width = (instruction & 0xF000) == 0x8000 ? 2 : instruction & 0x1000 ? 1 : 4;
      if (instruction & 0x800)
        assign(&regs, destination, false, 0);
      else if (known_register(&regs, source) &&
               !store_wait(view, offset, regs.value[source] + ((instruction >> 6) & 31) * width, width, true))
        return false;
    }
    else if ((instruction & 0xF000) == 0x9000 || (instruction & 0xF000) == 0xA000) {
      if ((instruction & 0xF000) == 0xA000 || (instruction & 0x800))
        assign(&regs, (instruction >> 8) & 7, false, 0);
    }
    else if ((instruction & 0xFE00) == 0xB400 || (instruction & 0xFF00) == 0xB000)
      assign(&regs, 13, false, 0);
    else if ((instruction & 0xFF00) == 0xBC00) {
      regs.known &= ~(instruction & 255);
      assign(&regs, 13, false, 0);
    }
    else if ((instruction & 0xF000) == 0xD000 && ((instruction >> 8) & 15) < 14 && !(instruction & 128)) {
      unsigned target = offset + 4 + (instruction & 255) * 2;
      if (target > view->size || target - origin > 1024 || join != view->size)
        break;
      branch_regs = regs;
      join = target;
    }
    else if ((instruction & 0xF800) == 0xF000 && offset + 4 <= join) {
      unsigned second = read_value(view, offset + 2, 2);
      int32_t displacement = (int32_t)((instruction & 0x7FF) << 21) >> 9;
      int32_t target = (int32_t)offset + 4 + displacement + (second & 0x7FF) * 2;
      unsigned clobbers;
      if ((second & 0xF800) != 0xF800 || target < 0 ||
          (unsigned)target + 4 > view->size ||
          !thumb_leaf_clobbers(view, target, &clobbers))
        break;
      regs.known &= ~clobbers;
      offset += 2;
    }
    else if ((instruction & 0xF800) == 0xE000 && !(instruction & 0x400)) {
      unsigned target = offset + 4 + (instruction & 0x7FF) * 2;
      if (target <= offset || target - origin > 1024 || target + 2 > view->size || target > join)
        break;
      offset = target - 2;
    }
    else
      break;
  }
  return true;
}

static bool scan_arm(const t_scan_view *view, unsigned start, unsigned pool,
                     unsigned initial_reg, uint32_t initial_value) {
  t_registers regs = {{0}, 0};
  assign(&regs, initial_reg, true, initial_value);
  for (unsigned offset = start + 4, steps = 0;
       offset + 4 <= view->size && offset - start <= 4096 && steps < 128; offset += 4, steps++) {
    if (offset == pool)
      break;
    uint32_t instruction = read_value(view, offset, 4);
    unsigned destination = (instruction >> 12) & 15;
    unsigned source = (instruction >> 16) & 15;
    bool word_memory = (instruction & 0x0C000000) == 0x04000000;
    bool half_memory = (instruction & 0x0E0000F0) == 0x000000B0;
    if (word_memory || half_memory) {
      if (!(instruction & 0x01000000) || (instruction & 0x00200000) || destination == 15)
        break;
      uint32_t displacement = word_memory ? instruction & 0xFFF :
                              ((instruction >> 4) & 0xF0) | (instruction & 15);
      bool register_offset = word_memory ? instruction & 0x02000000 : !(instruction & 0x00400000);
      bool address_known = known_register(&regs, source) && source != 15;
      if (register_offset) {
        unsigned operand = instruction & 15;
        if ((word_memory && (instruction & 0x10)) || (half_memory && (instruction & 0xF00)))
          break;
        address_known = address_known && operand != 15 && known_register(&regs, operand);
        displacement = regs.value[operand];
        if (word_memory)
          address_known = shifted_value(displacement, (instruction >> 5) & 3,
                                        (instruction >> 7) & 31, true, &displacement) && address_known;
      }
      uint32_t address = instruction & 0x00800000 ? regs.value[source] + displacement :
                                                                 regs.value[source] - displacement;
      if (!(instruction & 0x00100000)) {
        unsigned width = half_memory ? 2 : instruction & 0x00400000 ? 1 : 4;
        if (address_known && !store_wait(view, offset, address, width, false))
          return false;
      }
      else {
        if ((instruction >> 28) != 14)
          break;
        bool literal = word_memory && source == 15 && !register_offset && !(instruction & 0x00400000);
        unsigned target = instruction & 0x00800000 ? offset + 8 + displacement : offset + 8 - displacement;
        literal = literal && !(target & 3) && target <= view->size - 4;
        assign(&regs, destination, literal, literal ? read_value(view, target, 4) : 0);
      }
    }
    else if ((instruction >> 28) == 14 && (instruction & 0x0C000000) == 0 &&
             ((instruction & 0x02000000) || !(instruction & 0x90))) {
      unsigned kind = (instruction >> 21) & 15;
      if (kind >= 8 && kind <= 11) {
        if (!(instruction & 0x00100000))
          break;
        continue;
      }
      if (destination == 15)
        break;
      bool immediate = instruction & 0x02000000;
      unsigned operand = instruction & 15;
      bool known = immediate || known_register(&regs, operand);
      uint32_t value = immediate ? rotate_right(instruction & 255, ((instruction >> 8) & 15) * 2) : 0;
      if (!immediate)
        known = operand != 15 && shifted_value(regs.value[operand], (instruction >> 5) & 3,
                                               (instruction >> 7) & 31, true, &value) && known;
      if (kind != 13 && kind != 15)
        known = known && known_register(&regs, source);
      if (kind == 0)
        value &= regs.value[source];
      else if (kind == 2)
        value = regs.value[source] - value;
      else if (kind == 4)
        value += regs.value[source];
      else if (kind == 12)
        value |= regs.value[source];
      else if (kind == 14)
        value = regs.value[source] & ~value;
      else if (kind == 15)
        value = ~value;
      else if (kind != 13)
        known = false;
      assign(&regs, destination, known, value);
    }
    else if ((instruction & 0xFF000000) == 0xEA000000 && !(instruction & 0x800000)) {
      unsigned target = offset + 8 + (instruction & 0xFFFFFF) * 4;
      if (target <= offset || target - start > 4096 || target + 4 > view->size)
        break;
      offset = target - 4;
    }
    else
      break;
  }
  return true;
}

bool waitscan_process(const uint8_t *data, unsigned size, uint32_t base,
                      const uint8_t *prefix, unsigned prefix_size,
                      bool (*emit)(void *, uint32_t), void *context,
                      void (*progress)(unsigned)) {
  return waitscan_process_progress(data, size, base, prefix, prefix_size, emit, context, progress, 0, 1);
}

bool waitscan_process_progress(const uint8_t *data, unsigned size, uint32_t base,
                      const uint8_t *prefix, unsigned prefix_size,
                      bool (*emit)(void *, uint32_t), void *context,
                      void (*progress)(unsigned), unsigned progress_base, unsigned progress_divisor) {
  return waitscan_process_progress_indexed(data, size, base, prefix, prefix_size, emit, context, progress, progress_base, progress_divisor, NULL);
}

// Record only literal loads that reference an IO constant. Sort by the
// original traversal order (pool, Thumb / ARM positive / ARM negative, source).
// Workspace reserves three entries per word, including dense inputs.
static void reference_note(t_scan_index *index, const t_scan_view *view,
                           unsigned source, unsigned pool, unsigned kind) {
  if ((pool & 3) || pool + 4 > view->size || !io_address(read_value(view, pool, 4))) return;

  t_scan_reference *ref = index->references + index->reference_count++;
  ref->pool = pool;
  ref->source = source | (kind << 30);
}

static bool reference_less(t_scan_reference a, t_scan_reference b) {
  return a.pool < b.pool || (a.pool == b.pool && a.source < b.source);
}

static void reference_sift(t_scan_reference *items, unsigned root, unsigned count) {
  t_scan_reference value = items[root];
  while (root < count / 2) {
    unsigned child = root * 2 + 1;
    if (child + 1 < count && reference_less(items[child], items[child + 1])) child++;
    if (!reference_less(value, items[child])) break;
    items[root] = items[child];
    root = child;
  }
  items[root] = value;
}

static __attribute__((noinline)) void reference_build(t_scan_index *index, const t_scan_view *view) {
  index->reference_count = 0;
  for (unsigned cursor = 0, offset; view->size >= 4 &&
       scan_index_next(index, SCAN_REFERENCE, &cursor, 4, view->size - 4, &offset); ) {
    uint32_t word = read_value(view, offset, 4);
    for (unsigned half = 0; half < 2; half++) {
      unsigned instruction = (word >> (half * 16)) & 0xFFFF, source = offset + half * 2;
      if ((instruction & 0xF800) == 0x4800) {
        unsigned pool = offset + 4 + (instruction & 255) * 4;
        if (pool - source <= 1024) reference_note(index, view, source, pool, 0);
      }
    }
    if (((word >> 12) & 15) != 15) {
      if ((word & 0xFFFF0000) == 0xE59F0000) {
        unsigned pool = offset + 8 + (word & 0xFFF);
        if (pool - offset <= 4096) reference_note(index, view, offset, pool, 1);
      } else if ((word & 0xFFFF0000) == 0xE51F0000 && (word & 0xFFF) >= 12 &&
                 offset + 8 >= (word & 0xFFF)) {
        unsigned pool = offset + 8 - (word & 0xFFF);
        if (offset + 4 - pool <= 4096) reference_note(index, view, offset, pool, 2);
      }
    }
  }
  unsigned count = index->reference_count;
  for (unsigned root = count / 2; root; ) reference_sift(index->references, --root, count);
  while (count > 1) {
    t_scan_reference top = index->references[0];
    index->references[0] = index->references[--count];
    index->references[count] = top;
    reference_sift(index->references, 0, count);
  }
}

__attribute__((noinline)) bool waitscan_process_progress_indexed(const uint8_t *data, unsigned size, uint32_t base,
                      const uint8_t *prefix, unsigned prefix_size,
                      bool (*emit)(void *, uint32_t), void *context,
                      void (*progress)(unsigned), unsigned progress_base, unsigned progress_divisor, t_scan_index *index) {
  if (prefix_size > base || (base & 3) || (prefix_size & 3) || !emit)
    return false;
  t_scan_view view = {prefix, data, prefix_size, size + prefix_size, base - prefix_size, emit, context};
  if (view.size < 4)
    return true;
  if (index) reference_build(index, &view);
  unsigned reference_cursor = 0;
  for (unsigned candidate = 0, offset; view.size >= 4 &&
       scan_index_next(index, SCAN_WAIT, &candidate, 4, view.size - 4, &offset); ) {
    if (progress && !(offset & 0xFFFF) && offset >= prefix_size)
      progress(progress_base + (offset - prefix_size) / (4 * progress_divisor));
    uint32_t value = read_value(&view, offset, 4);
    if (io_address(value)) {
      if (index) {
        while (reference_cursor < index->reference_count && index->references[reference_cursor].pool < offset)
          reference_cursor++;
        while (reference_cursor < index->reference_count && index->references[reference_cursor].pool == offset) {
          unsigned encoded = index->references[reference_cursor++].source;
          unsigned source = encoded & 0x3FFFFFFF, kind = encoded >> 30;
          uint32_t instruction = read_value(&view, source, kind ? 4 : 2);
          if (kind ? !scan_arm(&view, source, offset, (instruction >> 12) & 15, value) :
                     !scan_thumb(&view, source, offset, (instruction >> 8) & 7, value)) return false;
        }
      } else {
      unsigned begin = offset > 1024 ? offset - 1024 : 0;
      for (unsigned source = begin; source + 2 <= offset; source += 2) {
        unsigned instruction = read_value(&view, source, 2);
        if ((instruction & 0xF800) == 0x4800 &&
            (source & ~3U) + 4 + (instruction & 255) * 4 == offset &&
            !scan_thumb(&view, source, offset, (instruction >> 8) & 7, value))
          return false;
      }
      begin = offset > 4096 ? offset - 4096 : 0;
      for (unsigned source = begin; source + 4 <= offset; source += 4) {
        uint32_t instruction = read_value(&view, source, 4);
        if ((instruction & 0xFFFF0000) == 0xE59F0000 && ((instruction >> 12) & 15) != 15 &&
            source + 8 + (instruction & 0xFFF) == offset &&
            !scan_arm(&view, source, offset, (instruction >> 12) & 15, value))
          return false;
      }
      unsigned end = offset + 4096 < view.size ? offset + 4096 : view.size;
      for (unsigned source = offset + 4; source + 4 <= end; source += 4) {
        uint32_t instruction = read_value(&view, source, 4);
        if ((instruction & 0xFFFF0000) == 0xE51F0000 && ((instruction >> 12) & 15) != 15 &&
            source + 8 - (instruction & 0xFFF) == offset &&
            !scan_arm(&view, source, offset, (instruction >> 12) & 15, value))
          return false;
      }
      }
    }
    uint32_t instruction_arm = read_value(&view, offset, 4);
    if ((instruction_arm & 0xFFFF0000) == 0xE3A00000 && ((instruction_arm >> 12) & 15) != 15) {
      uint32_t initial_value = rotate_right(instruction_arm & 255, ((instruction_arm >> 8) & 15) * 2);
      if (io_address(initial_value) &&
          !scan_arm(&view, offset, view.size, (instruction_arm >> 12) & 15, initial_value))
        return false;
    }
    for (unsigned source = offset; source < offset + 4 && source + 4 <= view.size; source += 2) {
      unsigned instruction = read_value(&view, source, 2);
      unsigned shift = read_value(&view, source + 2, 2);
      unsigned reg = (instruction >> 8) & 7;
      if (instruction == 0xB590 && !scan_thumb_wait_wrapper(&view, source))
        return false;
      if ((instruction & 0xF800) == 0x2000 && (shift & 0xF800) == 0 &&
          (shift & 7) == reg && ((shift >> 3) & 7) == reg &&
          io_address((uint32_t)(instruction & 255) << ((shift >> 6) & 31)) &&
          !scan_thumb(&view, source, view.size, reg, instruction & 255))
        return false;
    }
  }
  return true;
}
