#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H
#include <stdint.h>
#include <stdbool.h>
#include "patchengine.h"

#define MENU_LAYOUT_MAX_REGIONS 32
#define MENU_LAYOUT_FONT_RESERVE 8192
#define MENU_COMPACT_FONT_RESERVE 4096
typedef struct { uint32_t offset, length, address; } t_menu_region;
typedef struct {
  uint32_t id, anchor, font, count, total;
  t_menu_region regions[MENU_LAYOUT_MAX_REGIONS];
  uint32_t borrowed_count;
  uint32_t borrowed[8][3];  // physical address, bytes, original fill word
  bool compact;
} t_menu_layout;

const t_menu_layout *menu_layout_get(uint32_t id);
bool menu_layout_valid(const t_menu_layout *layout, unsigned payload, unsigned font);
bool menu_layout_available(const t_patch *patch, unsigned cheats);
#endif
