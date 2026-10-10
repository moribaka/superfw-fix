/* Bounded GBA import adapter for mGBA decoders. SPDX-License-Identifier: MPL-2.0
 * All persistent state is caller supplied. No heap or emulator dependency.
 * cbTable uses halfwords because byte stores cannot write cartridge SDRAM. */
#pragma once
#include "cheat_ops.h"
#include "mgba_types.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define COMPLETE ((size_t)-1)
#define OFFSET_MASK 0xFFFFFF
#define GBA_BASE_ROM0 0x08000000u
#define GBA_BASE_IO 0x04000000u
#define GBA_SIZE_ROM0 0x02000000u
#define GBA_REG_KEYINPUT 0x130
#define MODE_THUMB 1
#define ROR(v, n) (((uint32_t)(v) >> (n)) | ((uint32_t)(v) << (32 - (n))))
#define mLOG(...) ((void)0)
// The upstream formatting strings are unused diagnostics in these decoders.
#define snprintf(...) 0
enum { CHEAT_OP_LIMIT = 1800, CHEAT_PATCH_LIMIT = 32 };
struct mCheatList {
  struct mCheat *data;
  size_t size;
  uint32_t *failed;
};
struct mCheatPatchList {
  struct mCheatPatch *data;
  size_t size;
  uint32_t *failed;
};
struct GBACheatHook {
  uint32_t address, mode, patchedOpcode, refs, reentries;
};
struct GBACheatSet {
  struct {
    struct mCheatList list;
    struct mCheatPatchList romPatches;
  } d;
  size_t incompleteCheat;
  struct mCheatPatch *incompletePatch;
  size_t currentBlock;
  int gsaVersion;
  uint32_t gsaSeeds[4], cbRngState, cbMaster;
  uint16_t cbTable[48];
  uint32_t cbSeeds[4];
  int remainingAddresses;
  struct GBACheatHook *hook;
  struct GBACheatHook hookStorage;
};
#define malloc(n) (&cheats->hookStorage)
static inline struct mCheat *mCheatListAppend(struct mCheatList *l) {
  if (l->size >= CHEAT_OP_LIMIT) {
    *l->failed = 1;
    return &l->data[CHEAT_OP_LIMIT];
  }
  struct mCheat *p = &l->data[l->size++];
  *p = (struct mCheat){0};
  return p;
}
static inline struct mCheatPatch *mCheatPatchListAppend(struct mCheatPatchList *l) {
  if (l->size >= CHEAT_PATCH_LIMIT) {
    *l->failed = 1;
    return &l->data[CHEAT_PATCH_LIMIT];
  }
  struct mCheatPatch *p = &l->data[l->size++];
  *p = (struct mCheatPatch){0};
  return p;
}
static inline size_t mCheatListSize(const struct mCheatList *l) { return l->size; }
static inline struct mCheat *mCheatListGetPointer(struct mCheatList *l, size_t i) {
  if (i >= l->size) {
    *l->failed = 1;
    return &l->data[CHEAT_OP_LIMIT];
  }
  return &l->data[i];
}
static inline size_t mCheatListIndex(const struct mCheatList *l, const struct mCheat *p) {
  return p - l->data;
}
bool GBACheatAddCodeBreaker(struct GBACheatSet *, uint32_t, uint16_t);
bool GBACheatAddGameShark(struct GBACheatSet *, uint32_t, uint32_t);
bool GBACheatAddProActionReplay(struct GBACheatSet *, uint32_t, uint32_t);
int GBACheatAddressIsReal(uint32_t);
const char *hex32(const char *, uint32_t *);
const char *hex16(const char *, uint16_t *);
#include "gameshark.h"
#include "parv3.h"
