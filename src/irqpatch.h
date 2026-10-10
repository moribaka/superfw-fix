#ifndef IRQPATCH_H
#define IRQPATCH_H

#define IRQPATCH_BOOT_SIZE 1024
#define IRQPATCH_HOOK_OFFSET 1024
#define IRQPATCH_HOOK_SIZE 48
#define IRQPATCH_MAX_HOOKS 8
#define IRQPATCH_OPCODE 10
#define IRQPATCH_DMA_OPCODE 11
#define IRQPATCH_REGISTER_OPCODE 12
#define IRQPATCH_STORE_OPCODE 13
#define IRQPATCH_DEAD_ARG_OPCODE 14
#define IRQPATCH_NESTING_OPCODE 15
#define IRQPATCH_METADATA_OPCODE 6
#define IRQPATCH_BOOT_CLEAR 1
#define IRQPATCH_UNSUPPORTED 2
#define IRQPATCH_BOOT_RESTORE 4
#define IRQPATCH_ENTRY_CHECKS_SCANNED 8
#define IRQPATCH_ENTRY_CHECK_ARGUMENT 3
#define IRQPATCH_VBLANK_ARGUMENT 4

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>
#include "scan_index.h"

bool irqpatch_scan_boot(const uint8_t *rom, unsigned size, uint8_t *flags,
                        bool (*emit)(void *, const uint32_t *, unsigned), void *context);
bool irqpatch_scan_intro(const uint8_t *rom, unsigned size, unsigned romsize, uint8_t *flags,
                         bool (*emit)(void *, const uint32_t *, unsigned), void *context);
bool irqpatch_scan_dma(const uint8_t *data, unsigned size, uint32_t base,
                       const uint8_t *prefix, unsigned prefix_size, uint8_t *flags,
                       bool (*emit)(void *, const uint32_t *, unsigned), void *context);
bool irqpatch_scan_dma_progress(const uint8_t *data, unsigned size, uint32_t base,
                       const uint8_t *prefix, unsigned prefix_size, uint8_t *flags,
                       bool (*emit)(void *, const uint32_t *, unsigned), void *context, void (*progress)(unsigned));
bool irqpatch_scan_dma_progress_indexed(const uint8_t *data, unsigned size, uint32_t base,
                       const uint8_t *prefix, unsigned prefix_size, uint8_t *flags,
                       bool (*emit)(void *, const uint32_t *, unsigned), void *context, void (*progress)(unsigned), const t_scan_index *index);
bool irqpatch_scan_register(const uint8_t *data, unsigned size, uint32_t base,
                            const uint8_t *prefix, unsigned prefix_size, unsigned romsize,
                            uint8_t *flags,
                            bool (*emit)(void *, const uint32_t *, unsigned), void *context);
bool irqpatch_scan_register_progress(const uint8_t *data, unsigned size, uint32_t base,
                            const uint8_t *prefix, unsigned prefix_size, unsigned romsize,
                            uint8_t *flags, bool (*emit)(void *, const uint32_t *, unsigned),
                            void *context, void (*progress)(unsigned));
bool irqpatch_scan_register_progress_indexed(const uint8_t *data, unsigned size, uint32_t base,
                            const uint8_t *prefix, unsigned prefix_size, unsigned romsize,
                            uint8_t *flags, bool (*emit)(void *, const uint32_t *, unsigned),
                            void *context, void (*progress)(unsigned), const t_scan_index *index);
bool irqpatch_nesting_family(const uint8_t *rom, unsigned size);
bool irqpatch_scan_nesting(const uint8_t *data, unsigned size, uint32_t base,
                           const uint8_t *prefix, unsigned prefix_size,
                           bool (*emit)(void *, const uint32_t *, unsigned), void *context);
bool irqpatch_scan_nesting_indexed(const uint8_t *data, unsigned size, uint32_t base,
                           const uint8_t *prefix, unsigned prefix_size,
                           bool (*emit)(void *, const uint32_t *, unsigned), void *context, const t_scan_index *index);
bool irqpatch_apply_nesting(uint8_t *buffer, unsigned size, uint32_t base,
                            const uint32_t *operations, unsigned count);
bool irqpatch_native_present(const uint32_t *operations, unsigned count);
bool irqpatch_native_program_present(const uint32_t *operations, unsigned count, unsigned program_mask);
bool irqpatch_hook_count(const uint32_t *operations, unsigned count, unsigned *hooks);
void irqpatch_retarget_entry_checks(uint8_t *buffer, unsigned size, const uint32_t *operations,
                                   unsigned count, uint32_t patched_entry);
void irqpatch_retarget_entry_checks_block(uint8_t *buffer, unsigned size, uint32_t base,
                                         const uint32_t *operations, unsigned count, uint32_t patched_entry);
bool irqpatch_apply(uint8_t *buffer, unsigned size, uint32_t base, const uint32_t *operations,
                    unsigned count, uint32_t menu_address, unsigned handler_offset);

bool irqpatch_apply_vblank(uint8_t *buffer, unsigned size, uint32_t base, const uint32_t *operations,
                           unsigned count, uint32_t entry, uint32_t hotkey);
#endif

#endif
