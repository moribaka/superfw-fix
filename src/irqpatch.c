#pragma GCC optimize ("Oz")
#include "irqpatch.h"
#include "compiler.h"
#include "scan_read.h"
#include "scan_index.h"
#include "vblank_bridge.h"
#ifdef __GBA__
#include "common.h"
#endif
#include <string.h>

static void write_word(uint8_t *buffer, unsigned size, uint32_t base, uint32_t offset, uint32_t value);

static void write_byte(uint8_t *destination, uint8_t value) {
#ifdef __GBA__
  // SDRAM on the cartridge ignores byte stores. Preserve the adjacent byte
  // while using a halfword transaction, including clipped/unaligned patches.
  uintptr_t address = (uintptr_t)destination;
  volatile uint16_t *halfword = (volatile uint16_t*)(address & ~(uintptr_t)1);
  unsigned shift = (address & 1) * 8;
  *halfword = (*halfword & ~(0xFFU << shift)) | ((unsigned)value << shift);
#else
  *destination = value;
#endif
}

static uint32_t word(const uint8_t *rom, unsigned offset) {
  uint32_t result;
  memcpy(&result, rom + offset, sizeof(result));
  return result;
}

static uint32_t immediate(uint32_t instruction) {
  unsigned rotation = ((instruction >> 8) & 15) * 2;
  uint32_t value = instruction & 255;
  return rotation ? (value >> rotation) | (value << (32 - rotation)) : value;
}

static bool move(uint32_t instruction, uint32_t value, unsigned *destination) {
  if ((instruction & 0xFFFF0000) != 0xE3A00000 || immediate(instruction) != value)
    return false;
  *destination = (instruction >> 12) & 15;
  return *destination < 13;
}

static bool literal(uint32_t instruction, unsigned offset, unsigned size, unsigned *pool) {
  if ((instruction & 0xFFFF0000) != 0xE59F0000 &&
      (instruction & 0xFFFF0000) != 0xE51F0000)
    return false;
  int64_t address = (int64_t)offset + 8;
  address += instruction & 0x00800000 ? (instruction & 4095) : -(int64_t)(instruction & 4095);
  if (address < 0 || (address & 3) || address + 4 > size || ((instruction >> 12) & 15) >= 13)
    return false;
  *pool = (unsigned)address;
  return true;
}

static int64_t branch_target(uint32_t instruction, unsigned offset) {
  int32_t displacement = (int32_t)(instruction << 8) >> 6;
  return (int64_t)offset + 8 + displacement;
}

static bool back_branch(uint32_t instruction, unsigned offset, unsigned target, unsigned condition) {
  return (instruction & 0xFF000000) == (condition << 28 | 0x0A000000) &&
         branch_target(instruction, offset) == target;
}

static unsigned clear_end(const uint8_t *rom, unsigned size, unsigned offset) {
  if (offset + 28 > size)
    return 0;
  unsigned address, count, value, pool;
  if (literal(word(rom, offset), offset, size, &pool) &&
      ((word(rom, offset) >> 12) & 15) == 0 &&
      (word(rom, pool) == 0x03000200 || word(rom, pool) == 0x03000400) &&
      move(word(rom, offset + 4), 0x8000, &count) && count == 1 &&
      word(rom, offset + 8) == 0xE2411C02 &&
      (word(rom, offset + 12) & 0xFF000000) == 0xEB000000) {
    int64_t target = branch_target(word(rom, offset + 12), offset + 12);
    static const uint32_t function[] = {0xE3510000, 0x0A000003, 0xE3A02000,
                                       0xE8A00004, 0xE2511004, 0x1AFFFFFC, 0xE12FFF1E};
    if (target >= 0 && target + sizeof(function) <= size &&
        !memcmp(rom + target, function, sizeof(function)))
      return offset + 16;
  }
  if (!move(word(rom, offset), 0x03000000, &address))
    return 0;
  uint32_t load = word(rom, offset + 4);
  if (literal(load, offset + 4, size, &pool) && word(rom, pool) == 0x03008000 &&
      move(word(rom, offset + 8), 0, &value)) {
    count = (load >> 12) & 15;
    if (address != count && address != value && count != value &&
        word(rom, offset + 12) == (0xE4800004 | address << 16 | value << 12) &&
        word(rom, offset + 16) == (0xE1500000 | address << 16 | count) &&
        (back_branch(word(rom, offset + 20), offset + 20, offset + 12, 11) ||
         back_branch(word(rom, offset + 20), offset + 20, offset + 12, 3)))
      return offset + 24;
  }
  if (move(word(rom, offset + 4), 0, &value) &&
      move(word(rom, offset + 8), 0x8000, &count) &&
      address != count && address != value && count != value &&
      word(rom, offset + 12) == (0xE8A00000 | address << 16 | 1U << value) &&
      word(rom, offset + 16) == (0xE2500004 | count << 16 | count << 12) &&
      back_branch(word(rom, offset + 20), offset + 20, offset + 12, 1))
    return offset + 24;
  if (move(word(rom, offset + 4), 0x8000, &count) &&
      move(word(rom, offset + 8), 0, &value) &&
      address != count && address != value && count != value &&
      word(rom, offset + 12) == (0xE1C000B0 | address << 16 | value << 12) &&
      word(rom, offset + 16) == (0xE2800002 | address << 16 | address << 12) &&
      word(rom, offset + 20) == (0xE2500002 | count << 16 | count << 12) &&
      back_branch(word(rom, offset + 24), offset + 24, offset + 12, 1))
    return offset + 28;
  if (address == 0 && move(word(rom, offset + 4), 0x8000, &count) && count == 1 &&
      move(word(rom, offset + 8), 0, &value) && value == 2 &&
      (word(rom, offset + 12) & 0xFF000000) == 0xEB000000) {
    int64_t target = branch_target(word(rom, offset + 12), offset + 12);
    static const uint32_t function[] = {0xE92D0007, 0xE4802004, 0xE2511004,
                                       0x1AFFFFFC, 0xE8BD0007, 0xE12FFF1E};
    if (target >= 0 && target + sizeof(function) <= size &&
        !memcmp(rom + target, function, sizeof(function)))
      return offset + 16;
  }
  return 0;
}

static bool replayable(uint32_t instruction) {
  unsigned destination = (instruction >> 12) & 15;
  return destination < 13 && ((instruction & 0xFFFF0000) == 0xE59F0000 ||
                             (instruction & 0xFFFF0000) == 0xE51F0000 ||
                             (instruction & 0xFFFF0000) == 0xE3A00000);
}

NOINLINE bool irqpatch_scan_boot(const uint8_t *rom, unsigned size, uint8_t *flags,
                        bool (*emit)(void *, const uint32_t *, unsigned), void *context) {
  if (size < 192 || (word(rom, 0) & 0xFF000000) != 0xEA000000)
    return true;
  int64_t entry = branch_target(word(rom, 0), 0);
  if (entry < 192 || entry + 4 > size || (entry & 3))
    return true;
  for (unsigned offset = (unsigned)entry; offset + 4 <= size; offset += 4) {
    uint32_t instruction = word(rom, offset);
    if ((instruction & 0x0FFFFFF0) == 0x012FFF10 ||
        ((instruction & 0x0C10F000) == 0x0410F000))
      break;
    if ((instruction & 0xFF000000) == 0xEA000000)
      break;
    unsigned end = clear_end(rom, size, offset);
    if (!end)
      continue;
    *flags |= IRQPATCH_BOOT_CLEAR;
    if (end + 4 > size || !replayable(word(rom, end))) {
      *flags |= IRQPATCH_UNSUPPORTED;
      continue;
    }
    uint32_t original = word(rom, end);
    uint32_t value = 0;
    unsigned pool;
    if ((original & 0xFFFF0000) != 0xE3A00000) {
      if (!literal(original, end, size, &pool)) {
        *flags |= IRQPATCH_UNSUPPORTED;
        continue;
      }
      value = word(rom, pool);
      if (value == 0x03007FFC || value == 0x04000204) {
        *flags |= IRQPATCH_UNSUPPORTED;
        continue;
      }
    }
    const uint32_t operation[] = {IRQPATCH_OPCODE << 28 | end, original, value};
    if (!emit(context, operation, 3))
      return false;
    offset = end - 4;
  }
  return true;
}

bool irqpatch_scan_intro(const uint8_t *rom, unsigned size, unsigned romsize, uint8_t *flags,
                         bool (*emit)(void *, const uint32_t *, unsigned), void *context) {
  if (size < 192 || size > IRQPATCH_BOOT_SIZE || romsize < size || romsize > 32 * 1024 * 1024 ||
      (word(rom, 0) & 0xFF000000) != 0xEA000000)
    return true;
  int64_t entry = branch_target(word(rom, 0), 0);
  if (entry < size || entry >= romsize || (entry & 3))
    return true;
  for (unsigned offset = 192; offset + 64 <= size; offset += 4) {
    if (word(rom, offset) != 0xE3A00012 || word(rom, offset + 4) != 0xE129F000 ||
        word(rom, offset + 8) != 0xE59FD028 || word(rom, offset + 12) != 0xE3A0001F ||
        word(rom, offset + 16) != 0xE129F000 || word(rom, offset + 20) != 0xE59FD018 ||
        word(rom, offset + 28) != 0xE28F0018 || word(rom, offset + 32) != 0xE5810000 ||
        word(rom, offset + 40) != 0xE1A0E00F || word(rom, offset + 44) != 0xE12FFF11 ||
        (word(rom, offset + 48) & 0xFF000000) != 0xEA000000)
      continue;
    int64_t restart = branch_target(word(rom, offset + 48), offset + 48);
    if (restart < 192 || restart >= romsize || (restart & 3))
      continue;
    uint32_t user_stack = word(rom, offset + 52), irq_stack = word(rom, offset + 56);
    if ((user_stack & 3) || (irq_stack & 3) || user_stack < 0x03000008 ||
        user_stack > 0x03007FF4 || irq_stack < 0x03000008 || irq_stack > 0x03007FF4)
      continue;
    unsigned vector_pool, main_pool;
    uint32_t vector_load = word(rom, offset + 24), main_load = word(rom, offset + 36);
    if (((vector_load >> 12) & 15) != 1 || ((main_load >> 12) & 15) != 1 ||
        !literal(vector_load, offset + 24, size, &vector_pool) ||
        !literal(main_load, offset + 36, size, &main_pool) || word(rom, vector_pool) != 0x03007FFC)
      continue;
    uint32_t main_address = word(rom, main_pool);
    if (!(main_address & 1) || main_address < 0x080000C1 ||
        (main_address & ~1U) - 0x08000000 >= romsize)
      continue;
    const uint32_t operation[] = {IRQPATCH_OPCODE << 28 | (offset + 36), main_load, main_address};
    if (!emit(context, operation, 3))
      return false;
    *flags |= IRQPATCH_BOOT_RESTORE;
  }
  return true;
}

static bool thumb_target(uint32_t instruction, unsigned site, unsigned *target) {
  if ((instruction & 0xF800F800) != 0xF800F000)
    return false;
  int32_t displacement = ((instruction & 2047) << 12) | ((instruction >> 16 & 2047) << 1);
  if (displacement & (1 << 22))
    displacement -= 1 << 23;
  int64_t address = (int64_t)site + 4 + displacement;
  if (address < 192 || address >= 32 * 1024 * 1024 || (address & 1))
    return false;
  *target = (unsigned)address;
  return true;
}

typedef struct {
  const uint8_t *prefix;
  const uint8_t *data;
  unsigned prefix_size;
  unsigned size;
  unsigned base;
} t_irq_view;

static inline __attribute__((always_inline)) unsigned view_read(const t_irq_view *view, unsigned offset, unsigned size) {
  return scan_read(view->data, view->prefix, view->prefix_size, offset, size);
}

static const uint32_t nesting_dispatcher[] = {
  0xE3A03301, 0xE2833C02, 0xE5932000, 0xE1A01802, 0xE1A01821, 0xE14F0000,
  0xE92D400B, 0xE0021822, 0xE2110A02, 0x1AFFFFFE, 0xE3A02000, 0xE2110001,
  0x1A000026, 0xE2822004, 0xE2110002, 0x1A000023, 0xE2822004, 0xE2110004,
  0x1A000020, 0xE2822004, 0xE2110008, 0x1A00001D, 0xE2822004, 0xE2110010,
  0x1A00001A, 0xE2822004, 0xE2110020, 0x1A000017, 0xE2822004, 0xE2110040,
  0x1A000014, 0xE2822004, 0xE2110080, 0x1A000011, 0xE2822004, 0xE2110C01,
  0x1A00000E, 0xE2822004, 0xE2110C02, 0x1A00000B, 0xE2822004, 0xE2110B01,
  0x1A000008, 0xE2822004, 0xE2110B02, 0x1A000005, 0xE2822004, 0xE2110A01,
  0x1A000002, 0xE2822004, 0xE2110A02, 0x1AFFFFFE, 0xE1C300B2, 0xE10F3000,
  0xE3C330DF, 0xE383301F, 0xE129F003, 0xE59F103C, 0xE0811002, 0xE5910000,
  0xE92D4000, 0xE28FE000, 0xE12FFF10, 0xE8BD4000, 0xE10F3000, 0xE3C330DF,
  0xE3833092, 0xE129F003, 0xE8BD400B, 0xE1C310B0, 0xE169F000, 0xE12FFF1E
};

static bool nesting_match(const t_irq_view *view, unsigned offset) {
  if (offset > view->size || sizeof(nesting_dispatcher) > view->size - offset)
    return false;
  for (unsigned index = 0; index < sizeof(nesting_dispatcher) / 4; index++)
    if (view_read(view, offset + index * 4, 4) != nesting_dispatcher[index])
      return false;
  return true;
}

bool irqpatch_nesting_family(const uint8_t *rom, unsigned size) {
  if (size < 0x228)
    return false;
  const t_irq_view view = {NULL, rom, 0, size, 0};
  uint32_t table = word(rom, 0x224);
  return (table == 0x03002FE0 || table == 0x03002AF0) && nesting_match(&view, 0xFC);
}

bool irqpatch_scan_nesting(const uint8_t *data, unsigned size, uint32_t base,
                           const uint8_t *prefix, unsigned prefix_size,
                           bool (*emit)(void *, const uint32_t *, unsigned), void *context) {
  return irqpatch_scan_nesting_indexed(data, size, base, prefix, prefix_size, emit, context, NULL);
}

NOINLINE bool irqpatch_scan_nesting_indexed(const uint8_t *data, unsigned size, uint32_t base,
                           const uint8_t *prefix, unsigned prefix_size,
                           bool (*emit)(void *, const uint32_t *, unsigned), void *context, const t_scan_index *index) {
  if ((base & 3) || (prefix_size & 3) || prefix_size > base ||
      base >= 32 * 1024 * 1024 || size > 32 * 1024 * 1024 - base)
    return false;
  const t_irq_view view = {prefix, data, prefix_size, prefix_size + size, base - prefix_size};
  for (unsigned candidate = 0, offset; view.size >= sizeof(nesting_dispatcher) &&
       scan_index_next(index, SCAN_NEST, &candidate, 4, view.size - sizeof(nesting_dispatcher), &offset); ) {
    if (offset + sizeof(nesting_dispatcher) <= prefix_size || view.base + offset < 192 ||
        !nesting_match(&view, offset))
      continue;
    const uint32_t operation[] = {IRQPATCH_NESTING_OPCODE << 28 | (view.base + offset + 0xDC), 0xE383301F};
    if (!emit(context, operation, 2))
      return false;
  }
  return true;
}

static bool thumb_pool(const t_irq_view *view, unsigned offset, unsigned reg, unsigned *pool) {
  unsigned instruction = view_read(view, offset, 2);
  if ((instruction & 0xFF00) != (0x4800 | reg << 8))
    return false;
  unsigned address = ((view->base + offset) & ~3U) + 4 + (instruction & 255) * 4;
  if (address < view->base || address - view->base > view->size - 4)
    return false;
  *pool = address - view->base;
  return true;
}

bool irqpatch_scan_dma(const uint8_t *data, unsigned size, uint32_t base,
                       const uint8_t *prefix, unsigned prefix_size, uint8_t *flags,
                       bool (*emit)(void *, const uint32_t *, unsigned), void *context) {
  return irqpatch_scan_dma_progress(data, size, base, prefix, prefix_size, flags, emit, context, NULL);
}

bool irqpatch_scan_dma_progress(const uint8_t *data, unsigned size, uint32_t base,
                       const uint8_t *prefix, unsigned prefix_size, uint8_t *flags,
                       bool (*emit)(void *, const uint32_t *, unsigned), void *context, void (*progress)(unsigned)) {
  return irqpatch_scan_dma_progress_indexed(data, size, base, prefix, prefix_size, flags, emit, context, progress, NULL);
}

NOINLINE bool irqpatch_scan_dma_progress_indexed(const uint8_t *data, unsigned size, uint32_t base,
                       const uint8_t *prefix, unsigned prefix_size, uint8_t *flags,
                       bool (*emit)(void *, const uint32_t *, unsigned), void *context, void (*progress)(unsigned), const t_scan_index *index) {
  if ((base & 3) || (prefix_size & 3) || prefix_size > base ||
      base >= 32 * 1024 * 1024 || size > 32 * 1024 * 1024 - base)
    return false;
  t_irq_view view = {prefix, data, prefix_size, prefix_size + size, base - prefix_size};
  for (unsigned candidate = 0, offset; view.size >= 28 &&
       scan_index_next(index, SCAN_DMA, &candidate, 2, view.size - 28, &offset); ) {
    if (progress && offset >= prefix_size && !((offset - prefix_size) & 0xFFFF))
      progress((offset - prefix_size) / 16);
    if (view_read(&view, offset, 2) != 0xB500 || view_read(&view, offset + 2, 2) != 0xB081)
      continue;
    static const uint16_t sequence[] = {0x9000, 0, 0x4668, 0x6008, 0, 0x6048, 0, 0x6088, 0x6888};
    bool matches = true;
    for (unsigned index = 0; index < sizeof(sequence) / 2; index++)
      if (sequence[index] && view_read(&view, offset + 6 + index * 2, 2) != sequence[index])
        matches = false;
    unsigned pools[4];
    if (!matches || !thumb_pool(&view, offset + 4, 0, &pools[0]) ||
        !thumb_pool(&view, offset + 8, 1, &pools[1]) ||
        !thumb_pool(&view, offset + 14, 0, &pools[2]) ||
        !thumb_pool(&view, offset + 18, 0, &pools[3]))
      continue;
    if (view_read(&view, pools[1], 4) != 0x040000D4)
      continue;
    uint32_t destination = view_read(&view, pools[2], 4);
    uint32_t control = view_read(&view, pools[3], 4);
    unsigned words = control & 0xFFFF;
    if ((control & 0xFFFF0000) != 0x85000000 || !words || (destination & 3) ||
        destination < 0x03000000 || destination > 0x03007FF4 ||
        words > (0x03008000 - destination) / 4 || destination + words * 4 < 0x03008000)
      continue;
    unsigned available = offset + 28;
    for (unsigned index = 0; index < 4; index++)
      if (pools[index] + 4 > available)
        available = pools[index] + 4;
    if (available <= prefix_size)
      continue;
    unsigned site = view.base + offset + 24, target;
    uint32_t original = view_read(&view, offset + 24, 4);
    *flags |= IRQPATCH_BOOT_CLEAR;
    if (!thumb_target(original, site, &target)) {
      *flags |= IRQPATCH_UNSUPPORTED;
      continue;
    }
    const uint32_t operation[] = {IRQPATCH_DMA_OPCODE << 28 | site, original, target};
    if (!emit(context, operation, 3))
      return false;
  }
  return true;
}

static bool dead_arg_replay(const uint32_t *operations) {
  unsigned site = operations[0] & 0x1FFFFFF, subtract = operations[1];
  return !(site & 3) && site >= 192 && site <= 32 * 1024 * 1024 - 8 &&
         !(subtract & 1) && subtract >= 192 && subtract <= 32 * 1024 * 1024 - 2 &&
         (subtract + 2 <= site || subtract >= site + 8) &&
         (operations[2] & 0xFF00FFFF) == 0x4E00B570 &&
         (operations[3] & 0xFF00FF00) == 0x4D004C00 &&
         !(operations[4] & 3) && operations[4] >= 0x03000000 && operations[4] <= 0x03007FF4 &&
         !(operations[5] & 3) && !(operations[6] & 3) && operations[6] >= 0x080000C0 &&
         operations[5] >= operations[6] && operations[5] < 0x0A000000;
}

static bool register_replay(const uint32_t *operations) {
  return (operations[1] & 0xFFFF0000) == 0x1A240000 &&
         (operations[1] & 0xFF00) == 0x4800 && operations[2] == 0x04AD25C0 &&
         operations[3] == 0x0AD202A2 && (operations[4] & 3) == 0 &&
         operations[4] >= 0x080000C0 && operations[4] < 0x0A000000;
}

static bool thumb_store_replay(const uint32_t *operations) {
  if (((operations[0] >> 25) & 7) == 3) {
    bool register_epilogue = (operations[1] & 0xFFFFFF00) == 0x601C4B00 &&
                             (operations[2] & 0xFFFFFF00) == 0x801A4B00 &&
                             operations[5] == 0x04000208;
    bool flag_epilogue = (operations[1] & 0xFFFFFF00) == 0x60134A00 &&
                         operations[2] == 0x70232301;
    return operations[3] == 0xBC01BC10 && (register_epilogue || flag_epilogue) &&
           (operations[4] == 0x03FFFFFC || operations[4] == 0x03007FFC);
  }
  unsigned first = operations[1] & 0xFFFF, store = operations[1] >> 16;
  if ((first & 0xF800) != 0x4800 || (store & 0xF800) != 0x6000 ||
      ((store >> 3) & 7) != ((first >> 8) & 7) || ((store >> 6) & 31) != 15 ||
      operations[4] != 0x03007FC0)
    return false;
  unsigned literals = 0;
  for (unsigned index = 0; index < 6; index++) {
    unsigned instruction = (operations[1 + index / 2] >> ((index & 1) * 16)) & 0xFFFF;
    if ((instruction & 0xF800) == 0x4800) {
      if (++literals > 4)
        return false;
    }
    else if (instruction >= 0x4400 && (instruction & 0xF000) != 0x5000 &&
             (instruction & 0xE000) != 0x6000 && (instruction & 0xE000) != 0x8000)
      return false;
  }
  return true;
}

static bool store_replay(const uint32_t *operations) {
  if (((operations[0] >> 25) & 7) != 0)
    return thumb_store_replay(operations);
  uint32_t original = operations[1];
  uint32_t shape = original & 0xFFF00FFF;
  return (shape == 0xE5800FFC || shape == 0xE5800000 || shape == 0xE5000004) &&
         ((original >> 16) & 15) < 13 && ((original >> 12) & 15) < 13 &&
         operations[2] == 0x03007FFC;
}

static bool preserves_base(uint32_t instruction, unsigned address) {
  if ((instruction & 0xFFFF0000) == 0xE59F0000 ||
      (instruction & 0xFFFF0000) == 0xE51F0000 ||
      (instruction & 0xFFFF0000) == 0xE3A00000)
    return ((instruction >> 12) & 15) < 13 && ((instruction >> 12) & 15) != address;
  return (instruction & 0xFFF00000) == 0xE5800000 &&
         ((instruction >> 16) & 15) < 13 && ((instruction >> 12) & 15) < 13;
}

// Collect in discovery order, then emit in the original pattern order so
// overlapping patterns and serialized patch records retain their semantics.
typedef struct {
  uint32_t words[128];
  uint32_t lengths[128], patterns[128];
  unsigned used, records, pattern;
} t_irq_matches;
#ifdef __GBA__
_Static_assert(sizeof(t_irq_matches) <= 4096, "IRQ scan arena capacity");
#endif

static bool collect_irq_match(void *context, const uint32_t *words, unsigned count) {
  t_irq_matches *matches = context;
  if (count > 128 - matches->used || matches->records == 128)
    return false;
  for (unsigned index = 0; index < count; index++)
    matches->words[matches->used + index] = words[index];
  matches->used += count;
  matches->lengths[matches->records] = count;
  matches->patterns[matches->records++] = matches->pattern;
  return true;
}

static NOINLINE bool scan_register_candidate(const t_irq_view *source, unsigned offset, unsigned pattern,
                                             unsigned romsize, uint8_t *flags, t_irq_matches *matches) {
  const t_irq_view view = *source;
  unsigned prefix_size = view.prefix_size;
  bool (*emit)(void *, const uint32_t *, unsigned) = collect_irq_match;
  void *context = matches;
  matches->pattern = pattern;
  switch (pattern) {
  case 0: { do {
    unsigned site = view.base + offset;
    if ((site & 3) || view_read(&view, offset + 8, 4) != 0xBC01BC10 ||
        view_read(&view, offset + 12, 2) != 0x4700)
      continue;
    uint32_t operations[8] = {IRQPATCH_STORE_OPCODE << 28 | 3U << 25 | site,
                              view_read(&view, offset, 4), view_read(&view, offset + 4, 4),
                              view_read(&view, offset + 8, 4)};
    unsigned register_base = (operations[1] >> 8) & 7, pools[2], available = offset + 14;
    if (!thumb_pool(&view, offset, register_base, pools) ||
        (pools[0] < offset + 12 && pools[0] + 4 > offset))
      continue;
    operations[4] = view_read(&view, pools[0], 4);
    if (pools[0] + 4 > available)
      available = pools[0] + 4;
    if (register_base == 3) {
      if (view_read(&view, offset - 6, 4) != 0xD0072C00 || view_read(&view, offset - 2, 2) != 0x2201 ||
          !thumb_pool(&view, offset + 4, 3, pools + 1) ||
          (pools[1] < offset + 12 && pools[1] + 4 > offset))
        continue;
      operations[5] = view_read(&view, pools[1], 4);
    }
    else if (register_base == 2) {
      if (!thumb_pool(&view, offset - 2, 3, pools + 1))
        continue;
      uint32_t handler = view_read(&view, pools[1], 4);
      if ((handler & 3) || handler < 0x03000000 || handler > 0x03007FF4)
        continue;
    }
    else
      continue;
    if (pools[1] + 4 > available)
      available = pools[1] + 4;
    if (available <= prefix_size || !thumb_store_replay(operations))
      continue;
    if (!emit(context, operations, 8))
      return false;
    *flags |= IRQPATCH_BOOT_RESTORE;
  
  } while (0); return true; }
  case 1: { do {
    unsigned site = view.base + offset;
    unsigned first = view_read(&view, offset, 2), store = view_read(&view, offset + 2, 2);
    if ((site & 3) || (first & 0xF800) != 0x4800 || (store & 0xF800) != 0x6000 ||
        ((store >> 6) & 31) != 15 || ((store >> 3) & 7) != ((first >> 8) & 7))
      continue;
    unsigned handler_pool;
    if (!thumb_pool(&view, offset - 2, store & 7, &handler_pool))
      continue;
    uint32_t handler = view_read(&view, handler_pool, 4);
    if ((handler & 3) || !((handler >= 0x03000000 && handler <= 0x03007FF4) ||
                          (handler >= 0x080000C0 && handler - 0x08000000 < romsize)))
      continue;
    uint32_t operations[8] = {IRQPATCH_STORE_OPCODE << 28 | 1U << 25 | site,
                              view_read(&view, offset, 4), view_read(&view, offset + 4, 4),
                              view_read(&view, offset + 8, 4)};
    unsigned available = offset + 12, literals = 0;
    bool valid = !(handler_pool < offset + 12 && handler_pool + 4 > offset);
    if (handler_pool + 4 > available)
      available = handler_pool + 4;
    for (unsigned index = 0; valid && index < 6; index++) {
      unsigned instruction = view_read(&view, offset + index * 2, 2);
      if ((instruction & 0xF800) != 0x4800)
        continue;
      unsigned pool;
      if (literals == 4 || !thumb_pool(&view, offset + index * 2,
                                      (instruction >> 8) & 7, &pool) ||
          (pool < offset + 12 && pool + 4 > offset)) {
        valid = false;
        break;
      }
      operations[4 + literals++] = view_read(&view, pool, 4);
      if (pool + 4 > available)
        available = pool + 4;
    }
    if (!valid || available <= prefix_size || !thumb_store_replay(operations))
      continue;
    if (!emit(context, operations, 8))
      return false;
    *flags |= IRQPATCH_BOOT_RESTORE;
  
  } while (0); return true; }
  case 2: { do {
    unsigned site = view.base + offset + 6;
    if ((site & 3) || view_read(&view, offset + 2, 2) != 0x6005 ||
        view_read(&view, offset + 18, 2) != 0x1C29)
      continue;
    unsigned pools[3], target;
    if (!thumb_pool(&view, offset, 0, &pools[0]) ||
        !thumb_pool(&view, offset + 4, 4, &pools[1]) ||
        !thumb_pool(&view, offset + 6, 0, &pools[2]) ||
        view_read(&view, pools[0], 4) != 0x03007FFC ||
        !thumb_target(view_read(&view, offset + 20, 4), view.base + offset + 20, &target) ||
        target >= romsize || (target >= site && target < site + 12))
      continue;
    uint32_t operations[] = {IRQPATCH_REGISTER_OPCODE << 28 | site,
                             view_read(&view, offset + 6, 4), view_read(&view, offset + 10, 4),
                             view_read(&view, offset + 14, 4), view_read(&view, pools[2], 4)};
    uint32_t end = view_read(&view, pools[1], 4);
    if (!register_replay(operations) || end < operations[4] || (end & 3) ||
        end - 0x08000000 > romsize || end - operations[4] > 0x7FF4)
      continue;
    bool aliases = false;
    unsigned available = offset + 24;
    for (unsigned index = 0; index < 3; index++) {
      if (pools[index] + 4 > available)
        available = pools[index] + 4;
      if (pools[index] < offset + 18 && pools[index] + 4 > offset + 6)
        aliases = true;
    }
    if (aliases || available <= prefix_size)
      continue;
    if (!emit(context, operations, 5))
      return false;
    *flags |= IRQPATCH_BOOT_RESTORE;
  
  } while (0); return true; }
  case 3: { do {
    if (view_read(&view, offset + 4, 2) != 0x6008 || view_read(&view, offset + 6, 2) != 0x3804 ||
        view_read(&view, offset + 10, 2) != 0x6001)
      continue;
    unsigned pools[6], target;
    if (!thumb_pool(&view, offset, 1, &pools[0]) || !thumb_pool(&view, offset + 2, 0, &pools[1]) ||
        !thumb_pool(&view, offset + 8, 1, &pools[2]) ||
        view_read(&view, pools[1], 4) != 0x03008000 ||
        (view_read(&view, pools[0], 4) & 3) || view_read(&view, pools[0], 4) < 0x03000000 ||
        view_read(&view, pools[0], 4) > 0x03007FF4 ||
        (view_read(&view, pools[2], 4) & 3) || view_read(&view, pools[2], 4) < 0x03000000 ||
        view_read(&view, pools[2], 4) > 0x03007FF4 ||
        !thumb_target(view_read(&view, offset + 12, 4), view.base + offset + 12, &target) ||
        target < view.base || target - view.base > view.size - 18)
      continue;
    unsigned callee = target - view.base, ignored;
    if (view_read(&view, callee + 8, 2) != 0x1B64 || view_read(&view, callee + 10, 2) != 0x2001 ||
        view_read(&view, callee + 12, 2) != 0x1C21 ||
        !thumb_target(view_read(&view, callee + 14, 4), target + 14, &ignored) || ignored >= romsize ||
        !thumb_pool(&view, callee + 2, 6, &pools[3]) ||
        !thumb_pool(&view, callee + 4, 4, &pools[4]) || !thumb_pool(&view, callee + 6, 5, &pools[5]))
      continue;
    const uint32_t operations[] = {IRQPATCH_DEAD_ARG_OPCODE << 28 | target, view.base + offset + 6,
                                   view_read(&view, callee, 4), view_read(&view, callee + 4, 4),
                                   view_read(&view, pools[3], 4), view_read(&view, pools[4], 4),
                                   view_read(&view, pools[5], 4)};
    unsigned available = callee + 18 > offset + 16 ? callee + 18 : offset + 16;
    bool aliases = false;
    for (unsigned index = 0; index < 6; index++) {
      if (pools[index] + 4 > available)
        available = pools[index] + 4;
      if ((pools[index] < callee + 8 && pools[index] + 4 > callee) ||
          (pools[index] < offset + 8 && pools[index] + 4 > offset + 6))
        aliases = true;
    }
    if (aliases || available <= prefix_size || !dead_arg_replay(operations) ||
        operations[5] - 0x08000000 > romsize)
      continue;
    if (!emit(context, operations, 7))
      return false;
    *flags |= IRQPATCH_BOOT_RESTORE;
  
  } while (0); return true; }
  case 4: { do {
    unsigned address;
    uint32_t first = view_read(&view, offset, 4);
    if ((first & 0xFFFF0000) != 0xE3A00000 || !move(first, 0x7000, &address))
      continue;
    unsigned store_offset = offset + 8;
    uint32_t add = 0xE2800403 | address << 16 | address << 12;
    if (view_read(&view, offset + 4, 4) != add) {
      if (offset + 16 > view.size || !preserves_base(view_read(&view, offset + 4, 4), address) ||
          view_read(&view, offset + 8, 4) != add)
        continue;
      store_offset += 4;
    }
    if (store_offset + 4 <= prefix_size)
      continue;
    const uint32_t operation[] = {IRQPATCH_STORE_OPCODE << 28 | (view.base + store_offset),
                                  view_read(&view, store_offset, 4), 0x03007FFC};
    if (!store_replay(operation) || (operation[1] & 4095) != 0xFFC ||
        ((operation[1] >> 16) & 15) != address)
      continue;
    if (!emit(context, operation, 3))
      return false;
    *flags |= IRQPATCH_BOOT_RESTORE;
  
  } while (0); return true; }
  case 5: { do {
    unsigned pool;
    uint32_t load = view_read(&view, offset, 4);
    if (((load & 0xFFFF0000) != 0xE59F0000 && (load & 0xFFFF0000) != 0xE51F0000) ||
        !literal(load, offset, view.size, &pool) || view_read(&view, pool, 4) != 0x03007000)
      continue;
    unsigned address = (load >> 12) & 15;
    for (unsigned distance = 4; distance <= 16 && offset + distance + 4 <= view.size; distance += 4) {
      uint32_t instruction = view_read(&view, offset + distance, 4);
      const uint32_t operation[] = {IRQPATCH_STORE_OPCODE << 28 | (view.base + offset + distance),
                                    instruction, 0x03007FFC};
      if (store_replay(operation) && (instruction & 4095) == 0xFFC &&
          ((instruction >> 16) & 15) == address) {
        unsigned available = offset + distance + 4;
        if (pool + 4 > available)
          available = pool + 4;
        if (available > prefix_size) {
          if (!emit(context, operation, 3))
            return false;
          *flags |= IRQPATCH_BOOT_RESTORE;
        }
        break;
      }
      if (!preserves_base(instruction, address))
        break;
    }
  
  } while (0); return true; }
  case 6: { do {
    unsigned address;
    uint32_t first = view_read(&view, offset, 4);
    if ((first & 0xFFFF0000) != 0xE3A00000 || !move(first, 0x03000000, &address))
      continue;
    uint32_t add = view_read(&view, offset + 4, 4);
    uint32_t subtract = view_read(&view, offset + 8, 4);
    if ((add & 0xFFF00000) != 0xE2800000 || immediate(add) != 0x8000 ||
        ((add >> 16) & 15) != address || ((add >> 12) & 15) != address ||
        (subtract & 0xFFF00000) != 0xE2400000 || immediate(subtract) != 4 ||
        ((subtract >> 16) & 15) != address || ((subtract >> 12) & 15) != address)
      continue;
    for (unsigned distance = 12; distance <= 24 && offset + distance + 4 <= view.size; distance += 4) {
      uint32_t instruction = view_read(&view, offset + distance, 4);
      const uint32_t operation[] = {IRQPATCH_STORE_OPCODE << 28 | (view.base + offset + distance),
                                   instruction, 0x03007FFC};
      if ((instruction & 4095) == 0 && store_replay(operation) &&
          ((instruction >> 16) & 15) == address) {
        if (offset + distance + 4 > prefix_size) {
          if (!emit(context, operation, 3))
            return false;
          *flags |= IRQPATCH_BOOT_RESTORE;
        }
        break;
      }
      if (!preserves_base(instruction, address))
        break;
    }
  
  } while (0); return true; }
  }
  return false;
}

bool irqpatch_scan_register(const uint8_t *data, unsigned size, uint32_t base,
                            const uint8_t *prefix, unsigned prefix_size, unsigned romsize,
                            uint8_t *flags,
                            bool (*emit)(void *, const uint32_t *, unsigned), void *context) {
  return irqpatch_scan_register_progress(data, size, base, prefix, prefix_size, romsize, flags, emit, context, NULL);
}

bool irqpatch_scan_register_progress(const uint8_t *data, unsigned size, uint32_t base,
                            const uint8_t *prefix, unsigned prefix_size, unsigned romsize,
                            uint8_t *flags, bool (*emit)(void *, const uint32_t *, unsigned),
                            void *context, void (*progress)(unsigned)) {
  return irqpatch_scan_register_progress_indexed(data, size, base, prefix, prefix_size, romsize, flags, emit, context, progress, NULL);
}

NOINLINE bool irqpatch_scan_register_progress_indexed(const uint8_t *data, unsigned size, uint32_t base,
                            const uint8_t *prefix, unsigned prefix_size, unsigned romsize,
                            uint8_t *flags, bool (*emit)(void *, const uint32_t *, unsigned),
                            void *context, void (*progress)(unsigned), const t_scan_index *index) {
  if ((base & 3) || (prefix_size & 3) || prefix_size > base ||
      romsize > 32 * 1024 * 1024 || base > romsize || size > romsize - base)
    return false;
  t_irq_view view = {prefix, data, prefix_size, prefix_size + size, base - prefix_size};
#ifdef __GBA__
  t_irq_matches *matches = (t_irq_matches*)ROM_SCAN_MATCHES_ADDR;
#else
  t_irq_matches storage;
  t_irq_matches *matches = &storage;
#endif
  matches->used = matches->records = matches->pattern = 0;
  uint32_t current_word = 0;
  for (unsigned candidate = 0, cursor; view.size >= 4 &&
       scan_index_next(index, SCAN_IRQ, &candidate, 2, view.size - 4, &cursor); ) {
    if (progress && cursor >= prefix_size && !((cursor - prefix_size) & 0xFFFF))
      progress(size / 16 + (cursor - prefix_size) / 16);
    if (!(cursor & 3))
      current_word = view_read(&view, cursor, 4);
    unsigned instruction = (current_word >> ((cursor & 2) * 8)) & 0xFFFF;
    if (!(cursor & 3)) {
      if (instruction == 0xBC10 && cursor >= 16 && cursor + 6 <= view.size &&
          !scan_register_candidate(&view, cursor - 8, 0, romsize, flags, matches)) return false;
      if ((instruction & 0xF800) == 0x4800 && cursor >= 4 && cursor + 12 <= view.size &&
          !scan_register_candidate(&view, cursor, 1, romsize, flags, matches)) return false;
      if (instruction == 0x6005 && cursor >= 4 && cursor + 22 <= view.size &&
          !scan_register_candidate(&view, cursor - 2, 2, romsize, flags, matches)) return false;
      if (cursor + 4 <= view.size) {
        uint32_t word = current_word;
        if ((word & 0xFFFF0000) == 0xE3A00000) {
          if (cursor + 12 <= view.size &&
              !scan_register_candidate(&view, cursor, 4, romsize, flags, matches)) return false;
          if (cursor + 16 <= view.size &&
              !scan_register_candidate(&view, cursor, 6, romsize, flags, matches)) return false;
        }
        if (((word & 0xFFFF0000) == 0xE59F0000 || (word & 0xFFFF0000) == 0xE51F0000) &&
            cursor + 8 <= view.size &&
            !scan_register_candidate(&view, cursor, 5, romsize, flags, matches)) return false;
      }
    }
    if (instruction == 0x6008 && cursor >= 4 && cursor + 12 <= view.size &&
        !scan_register_candidate(&view, cursor - 4, 3, romsize, flags, matches)) return false;
  }
  for (unsigned pattern = 0; pattern < 7; pattern++) {
    unsigned position = 0;
    for (unsigned record = 0; record < matches->records; record++) {
      unsigned count = matches->lengths[record];
      if (matches->patterns[record] == pattern && !emit(context, matches->words + position, count))
        return false;
      position += count;
    }
  }
  return true;
}

static unsigned operation_words(uint32_t operation) {
  unsigned opcode = operation >> 28, argument = (operation >> 25) & 7;
  switch (opcode) {
    case 0: return argument < 4 ? 1 : 0;
    case 1: case 2: return argument == 0 ? 1 : 0;
    case 3: return 1 + (argument + 4) / 4;
    case 4: return argument + 2;
    case 5: return argument == 0 || argument == 1 || argument == 4 || argument == 5 ? 1 : 0;
    case IRQPATCH_METADATA_OPCODE: return argument == IRQPATCH_VBLANK_ARGUMENT ? 3 :
      argument >= 1 && argument <= IRQPATCH_ENTRY_CHECK_ARGUMENT ? 1 : 0;
    case IRQPATCH_OPCODE: case IRQPATCH_DMA_OPCODE: return argument == 0 ? 3 : 0;
    case IRQPATCH_STORE_OPCODE: return argument == 0 ? 3 : argument == 1 || argument == 3 ? 8 : 0;
    case IRQPATCH_REGISTER_OPCODE: return argument == 0 ? 5 : 0;
    case IRQPATCH_DEAD_ARG_OPCODE: return argument == 0 ? 7 : 0;
    case IRQPATCH_NESTING_OPCODE: return argument == 0 ? 2 : 0;
    default: return 0;
  }
}

NOINLINE bool irqpatch_hook_count(const uint32_t *operations, unsigned count, unsigned *hooks) {
  *hooks = 0;
  for (unsigned cursor = 0; cursor < count; ) {
    unsigned words = operation_words(operations[cursor]);
    if (!words || words > count - cursor)
      return false;
    if (operations[cursor] >> 28 == IRQPATCH_OPCODE) {
      if ((operations[cursor] & 3) || (operations[cursor] & 0x1FFFFFF) >= IRQPATCH_BOOT_SIZE ||
          !replayable(operations[cursor + 1]) || ++*hooks > IRQPATCH_MAX_HOOKS)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_DMA_OPCODE) {
      unsigned site = operations[cursor] & 0x1FFFFFF, target;
      if ((site & 1) || site < 192 || site > 32 * 1024 * 1024 - 4 ||
          !thumb_target(operations[cursor + 1], site, &target) ||
          target != operations[cursor + 2] || ++*hooks > IRQPATCH_MAX_HOOKS)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_REGISTER_OPCODE) {
      unsigned site = operations[cursor] & 0x1FFFFFF;
      if ((site & 3) || site < 192 || site > 32 * 1024 * 1024 - 12 ||
          !register_replay(operations + cursor) || ++*hooks > IRQPATCH_MAX_HOOKS)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_STORE_OPCODE) {
      unsigned site = operations[cursor] & 0x1FFFFFF;
      bool thumb = ((operations[cursor] >> 25) & 7) != 0;
      *hooks += thumb ? 2 : 1;
      if ((site & 3) || site < 192 || site > 32 * 1024 * 1024 - (thumb ? 12 : 4) ||
          !store_replay(operations + cursor) || *hooks > IRQPATCH_MAX_HOOKS)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_DEAD_ARG_OPCODE) {
      if (!dead_arg_replay(operations + cursor) || ++*hooks > IRQPATCH_MAX_HOOKS)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_NESTING_OPCODE) {
      unsigned site = operations[cursor] & 0x1FFFFFF;
      if ((site & 3) || site < 192 || site > 32 * 1024 * 1024 - 4 || operations[cursor + 1] != 0xE383301F)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_METADATA_OPCODE &&
               ((operations[cursor] >> 25) & 7) == IRQPATCH_VBLANK_ARGUMENT) {
      unsigned site = operations[cursor] & 0x1FFFFFF, hook = operations[cursor + 2];
      if ((site & 3) || site < 192 || site > 32 * 1024 * 1024 - 4 || (hook & 3) ||
          hook < 192 || hook > 32 * 1024 * 1024 - 128 ||
          (site < hook + 128 && site + 4 > hook) ||
          operations[cursor + 1] < 0x080000C0 || operations[cursor + 1] >= 0x0A000000)
        return false;
    } else if (operations[cursor] >> 28 == IRQPATCH_METADATA_OPCODE &&
               ((operations[cursor] >> 25) & 7) == IRQPATCH_ENTRY_CHECK_ARGUMENT) {
      unsigned site = operations[cursor] & 0x1FFFFFF;
      if ((site & 3) || site < 192 || site > 32 * 1024 * 1024 - 4)
        return false;
    }
    cursor += words;
  }
  return true;
}

IWRAM_CODE __attribute__((optimize("Oz"), noinline))
void irqpatch_retarget_entry_checks(uint8_t *buffer, unsigned size, const uint32_t *operations,
                                   unsigned count, uint32_t patched_entry) {
  irqpatch_retarget_entry_checks_block(buffer, size, 0, operations, count, patched_entry);
}

IWRAM_CODE __attribute__((optimize("Oz"), noinline))
void irqpatch_retarget_entry_checks_block(uint8_t *buffer, unsigned size, uint32_t base,
                                         const uint32_t *operations, unsigned count, uint32_t patched_entry) {
  for (unsigned cursor = 0; cursor < count; cursor += operation_words(operations[cursor])) {
    if (operations[cursor] >> 28 != IRQPATCH_METADATA_OPCODE ||
        ((operations[cursor] >> 25) & 7) != IRQPATCH_ENTRY_CHECK_ARGUMENT)
      continue;
    unsigned site = operations[cursor] & 0x1FFFFFF;
    if (size >= 4 && site >= base && site - base <= size - 4) {
      volatile uint16_t *destination = (volatile uint16_t*)(buffer + site - base);
      destination[0] = patched_entry;
      destination[1] = patched_entry >> 16;
    }
  }
}

bool irqpatch_apply_nesting(uint8_t *buffer, unsigned size, uint32_t base,
                            const uint32_t *operations, unsigned count) {
  unsigned hooks;
  if ((base & 3) || (size & 3) || base >= 32 * 1024 * 1024 ||
      size > 32 * 1024 * 1024 - base || !irqpatch_hook_count(operations, count, &hooks))
    return false;
  for (unsigned cursor = 0; cursor < count; cursor += operation_words(operations[cursor])) {
    if (operations[cursor] >> 28 != IRQPATCH_NESTING_OPCODE)
      continue;
    unsigned site = operations[cursor] & 0x1FFFFFF;
    if (site < base || site - base >= size)
      continue;
    if (site - base > size - 4 || word(buffer, site - base) != operations[cursor + 1])
      return false;
  }
  for (unsigned cursor = 0; cursor < count; cursor += operation_words(operations[cursor]))
    if (operations[cursor] >> 28 == IRQPATCH_NESTING_OPCODE) {
      unsigned site = operations[cursor] & 0x1FFFFFF;
      if (site >= base && site - base < size) {
        uint32_t instruction = operations[cursor + 1] | 0x80;
        write_word(buffer, size, base, site, instruction);
      }
    }
  return true;
}

bool irqpatch_native_program_present(const uint32_t *operations, unsigned count, unsigned program_mask) {
  bool native = false;
  for (unsigned cursor = 0; cursor < count; ) {
    unsigned words = operation_words(operations[cursor]);
    if (!words || words > count - cursor)
      return false;
    if ((operations[cursor] >> 28 == IRQPATCH_METADATA_OPCODE &&
         ((operations[cursor] >> 25) & 7) == IRQPATCH_VBLANK_ARGUMENT) ||
        (operations[cursor] >> 28 == 0 &&
         (program_mask & (1U << ((operations[cursor] >> 25) & 7)))) ||
        (operations[cursor] >> 28 == IRQPATCH_STORE_OPCODE && store_replay(operations + cursor)) ||
        (operations[cursor] >> 28 == IRQPATCH_DEAD_ARG_OPCODE && dead_arg_replay(operations + cursor)))
      native = true;
    cursor += words;
  }
  return native;
}

bool irqpatch_native_present(const uint32_t *operations, unsigned count) {
  return irqpatch_native_program_present(operations, count, 1U << 1);
}

static void write_word(uint8_t *buffer, unsigned size, uint32_t base, uint32_t offset, uint32_t value) {
  for (unsigned byte = 0; byte < 4; byte++)
    if (offset + byte >= base && offset + byte - base < size)
      write_byte(buffer + offset + byte - base, value >> (byte * 8));
}

static bool encode_branch(uint32_t source, uint32_t destination, uint32_t *instruction) {
  int64_t displacement = (int64_t)destination - source - 8;
  if ((displacement & 3) || displacement < -(1 << 25) || displacement >= (1 << 25))
    return false;
  *instruction = 0xEA000000 | (((uint32_t)(displacement >> 2)) & 0xFFFFFF);
  return true;
}

static bool encode_thumb_call(uint32_t source, uint32_t destination, uint32_t *instruction) {
  int64_t displacement = (int64_t)destination - source - 4;
  if ((displacement & 1) || displacement < -(1 << 22) || displacement >= (1 << 22))
    return false;
  *instruction = (0xF000 | ((uint32_t)(displacement >> 12) & 2047)) |
                 (0xF800 | ((uint32_t)(displacement >> 1) & 2047)) << 16;
  return true;
}

bool irqpatch_apply(uint8_t *buffer, unsigned size, uint32_t base, const uint32_t *operations,
                    unsigned count, uint32_t menu_address, unsigned handler_offset) {
  unsigned hooks;
  if (!irqpatch_hook_count(operations, count, &hooks) ||
      base >= 32 * 1024 * 1024 || size > 32 * 1024 * 1024 - base || (handler_offset & 3) ||
      menu_address < 0x08000000 || menu_address >= 0x0A000000 || (menu_address & 1023) ||
      menu_address - 0x08000000 + IRQPATCH_HOOK_OFFSET + hooks * IRQPATCH_HOOK_SIZE > 32 * 1024 * 1024 ||
      handler_offset > 32 * 1024 * 1024 - (menu_address - 0x08000000) - 4)
    return false;
  unsigned checked = 0;
  for (unsigned cursor = 0; cursor < count; cursor += operation_words(operations[cursor])) {
    unsigned opcode = operations[cursor] >> 28;
    if (opcode != IRQPATCH_OPCODE && opcode != IRQPATCH_DMA_OPCODE && opcode != IRQPATCH_REGISTER_OPCODE && opcode != IRQPATCH_STORE_OPCODE && opcode != IRQPATCH_DEAD_ARG_OPCODE)
      continue;
    uint32_t site = operations[cursor] & 0x1FFFFFF;
    bool thumb_store = opcode == IRQPATCH_STORE_OPCODE && ((operations[cursor] >> 25) & 7) != 0;
    uint32_t hook = menu_address - 0x08000000 + IRQPATCH_HOOK_OFFSET + checked * IRQPATCH_HOOK_SIZE;
    checked += thumb_store ? 2 : 1;
    uint32_t encoded;
    if (!thumb_store && opcode != IRQPATCH_REGISTER_OPCODE && opcode != IRQPATCH_DEAD_ARG_OPCODE && (opcode != IRQPATCH_DMA_OPCODE ?
        (!encode_branch(site, hook, &encoded) || !encode_branch(hook + 24, site + 4, &encoded)) :
        !encode_thumb_call(site, hook, &encoded)))
      return false;
    unsigned source_bytes = thumb_store || opcode == IRQPATCH_REGISTER_OPCODE ? 12 : opcode == IRQPATCH_DEAD_ARG_OPCODE ? 8 : 4;
    for (unsigned byte = 0; byte < source_bytes; byte++)
      if (site + byte >= base && site + byte - base < size &&
          buffer[site + byte - base] != (uint8_t)(operations[cursor + (opcode == IRQPATCH_DEAD_ARG_OPCODE ? 2 : 1) + byte / 4] >> ((byte % 4) * 8)))
        return false;
    if (opcode == IRQPATCH_DEAD_ARG_OPCODE)
      for (unsigned byte = 0; byte < 2; byte++) {
        uint32_t subtract = operations[cursor + 1] + byte;
        if (subtract >= base && subtract - base < size &&
            buffer[subtract - base] != (uint8_t)(0x3804 >> (byte * 8)))
          return false;
      }
  }
  unsigned index = 0;
  for (unsigned cursor = 0; cursor < count; cursor += operation_words(operations[cursor])) {
    unsigned opcode = operations[cursor] >> 28;
    if (opcode != IRQPATCH_OPCODE && opcode != IRQPATCH_DMA_OPCODE && opcode != IRQPATCH_REGISTER_OPCODE && opcode != IRQPATCH_STORE_OPCODE && opcode != IRQPATCH_DEAD_ARG_OPCODE)
      continue;
    uint32_t site = operations[cursor] & 0x1FFFFFF;
    uint32_t original = operations[cursor + 1];
    bool thumb_store = opcode == IRQPATCH_STORE_OPCODE && ((operations[cursor] >> 25) & 7) != 0;
    uint32_t hook = menu_address - 0x08000000 + IRQPATCH_HOOK_OFFSET + index * IRQPATCH_HOOK_SIZE;
    index += thumb_store ? 2 : 1;
    uint32_t jump, resume;
    if (thumb_store) {
      uint16_t code[32] = {0xBC80};
      bool mirror = ((operations[cursor] >> 25) & 7) == 3;
      unsigned literals = 0;
      for (unsigned replay = 0; replay < 6; replay++) {
        uint16_t instruction = operations[cursor + 1 + replay / 2] >> ((replay & 1) * 16);
        if ((instruction & 0xF800) == 0x4800) {
          unsigned pool = 36 + literals * 4, address = 2 + replay * 2;
          instruction = (instruction & 0xFF00) | ((pool - ((address + 4) & ~3U)) / 4);
          uint32_t value = operations[cursor + 4 + literals++];
          if (mirror && replay == 0)
            value -= 8;
          memcpy((uint8_t*)code + pool, &value, 4);
        }
        if (replay == 1 && !mirror)
          instruction ^= 0x80;
        code[1 + replay] = instruction;
      }
      unsigned pool = 36 + literals * 4;
      unsigned position = 7;
      if (mirror && ((operations[cursor + 1] >> 8) & 7) == 2) {
        code[position++] = 0x4A00 | ((pool - 16) / 4);
        memcpy((uint8_t*)code + pool, operations + cursor + 4, 4);
        pool += 4;
      }
      code[position++] = 0xB403;
      code[position] = 0x4800 | ((pool - ((position * 2 + 4) & ~3U)) / 4);
      position++;
      code[position] = 0x4900 | ((pool + 4 - ((position * 2 + 4) & ~3U)) / 4);
      position++;
      code[position++] = 0x6001;
      code[position++] = 0xBC03;
      code[position++] = 0xB081;
      code[position++] = 0xB401;
      code[position] = 0x4800 | ((pool + 8 - ((position * 2 + 4) & ~3U)) / 4);
      position++;
      code[position++] = 0x9001;
      code[position++] = 0xBD01;
      if (position == 17)
        code[position] = 0x46C0;
      uint32_t values[] = {0x03007FFC, menu_address + handler_offset, 0x08000001 | (site + 12)};
      memcpy((uint8_t*)code + pool, values, sizeof(values));
      for (unsigned offset = 0; offset < sizeof(code); offset += 4)
        write_word(buffer, size, base, hook + offset, word((const uint8_t*)code, offset));
      write_word(buffer, size, base, site, 0x4F01B480);
      write_word(buffer, size, base, site + 4, 0x46C04738);
      write_word(buffer, size, base, site + 8, 0x08000001 | hook);
      continue;
    }
    if (opcode == IRQPATCH_DEAD_ARG_OPCODE) {
      const uint32_t code[] = {0x4805B408, 0x60034B05, 0xB570BC08, 0x4C054E04,
                               0x48064D05, 0x46C04700, 0x03007FFC, menu_address + handler_offset,
                               operations[cursor + 4], operations[cursor + 5], operations[cursor + 6],
                               0x08000001 | (site + 8)};
      for (unsigned offset = 0; offset < sizeof(code) / 4; offset++)
        write_word(buffer, size, base, hook + offset * 4, code[offset]);
      write_word(buffer, size, base, site, 0x47004800);
      write_word(buffer, size, base, site + 4, 0x08000001 | hook);
      for (unsigned byte = 0; byte < 2; byte++) {
        uint32_t subtract = operations[cursor + 1] + byte;
        if (subtract >= base && subtract - base < size)
          write_byte(buffer + subtract - base, 0x380C >> (byte * 8));
      }
      continue;
    }
    if (opcode == IRQPATCH_STORE_OPCODE) {
      if (!encode_branch(site, hook, &jump) || !encode_branch(hook + 24, site + 4, &resume))
        return false;
      uint32_t replay = original ^ ((original & 4095) ? 8 : 0x00800008);
      const uint32_t code[] = {replay, 0xE92D0003, 0xE59F0018, 0xE59F1018,
                               0xE5801000, 0xE8BD0003, resume, 0xE1A00000,
                               0xE1A00000, 0xE1A00000, 0x03007FFC, menu_address + handler_offset};
      for (unsigned offset = 0; offset < sizeof(code) / 4; offset++)
        write_word(buffer, size, base, hook + offset * 4, code[offset]);
      write_word(buffer, size, base, site, jump);
      continue;
    }
    if (opcode == IRQPATCH_REGISTER_OPCODE) {
      const uint32_t code[] = {0x4807B408, 0x60034B07, 0x4807BC08, 0x25C01A24,
                               0x02A204AD, 0xB0810AD2, 0x4804B403, 0xBD039002,
                               0x03007FFC, menu_address + handler_offset,
                               operations[cursor + 4], 0x08000001 | (site + 12)};
      for (unsigned offset = 0; offset < sizeof(code) / 4; offset++)
        write_word(buffer, size, base, hook + offset * 4, code[offset]);
      write_word(buffer, size, base, site, 0x47004800);
      write_word(buffer, size, base, site + 4, 0x08000001 | hook);
      write_word(buffer, size, base, site + 8, 0x46C046C0);
      continue;
    }
    if (opcode == IRQPATCH_DMA_OPCODE) {
      if (!encode_thumb_call(site, hook, &jump))
        return false;
      const uint32_t code[] = {0x4A05B40C, 0x60134B05, 0xB081BC0C, 0x4804B403,
                               0xBD039002, 0x46C046C0, 0x03007FFC, menu_address + handler_offset,
                               0x08000001 | operations[cursor + 2]};
      for (unsigned offset = 0; offset < sizeof(code) / 4; offset++)
        write_word(buffer, size, base, hook + offset * 4, code[offset]);
      write_word(buffer, size, base, site, jump);
      continue;
    }
    if (!encode_branch(site, hook, &jump) || !encode_branch(hook + 24, site + 4, &resume))
      return false;
    uint32_t replay = (original & 0xFFFF0000) == 0xE3A00000 ? original :
                      0xE59F0010 | (original & 0xF000);
    const uint32_t code[] = {0xE92D000C, 0xE59F2018, 0xE59F3018, 0xE5823000, 0xE8BD000C,
                             replay, resume, 0xE1A00000, 0xE1A00000, 0x03007FFC,
                             menu_address + handler_offset, operations[cursor + 2]};
    for (unsigned offset = 0; offset < sizeof(code) / 4; offset++)
      write_word(buffer, size, base, hook + offset * 4, code[offset]);
    write_word(buffer, size, base, site, jump);
  }
  return true;
}

// A native callback hook avoids adding latency to every HBlank interrupt.
// The copied bridge preserves the game's System-mode frame and enters the
// existing menu in IRQ mode; the menu retains the native IWRAM dispatcher.
bool irqpatch_apply_vblank(uint8_t *buffer, unsigned size, uint32_t base,
    const uint32_t *operations, unsigned count, uint32_t entry, uint32_t hotkey) {
  unsigned hooks;
  if (!irqpatch_hook_count(operations, count, &hooks)) return false;
  for (unsigned cursor = 0; cursor < count; cursor += operation_words(operations[cursor])) {
    if (operations[cursor] >> 28 != IRQPATCH_METADATA_OPCODE ||
        ((operations[cursor] >> 25) & 7) != IRQPATCH_VBLANK_ARGUMENT) continue;
    unsigned site = operations[cursor] & 0x1FFFFFF, hook = operations[cursor + 2];
    const uint32_t *code = vblank_bridge_sdram;
    unsigned length = sizeof(vblank_bridge_sdram);
    for (unsigned byte = 0; byte < 4; byte++)
      if (site + byte >= base && site + byte - base < size &&
          buffer[site + byte - base] != (uint8_t)(operations[cursor + 1] >> (byte * 8))) return false;
    for (unsigned byte = 0; byte < length; byte++)
      if (hook + byte >= base && hook + byte - base < size && buffer[hook + byte - base] != 0xFF) return false;
    for (unsigned offset = 0; offset < length; offset += 4) {
      uint32_t value = code[offset / 4];
      if (value == 0x11111111) value = hotkey;
      if (value == 0x22222222) value = entry;
      if (value == 0x33333333) value = operations[cursor + 1];
      write_word(buffer, size, base, hook + offset, value);
    }
    write_word(buffer, size, base, site, 0x08000000 + hook);
  }
  return true;
}
