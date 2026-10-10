#ifndef MENU_OVERLAY_PROFILE_H
#define MENU_OVERLAY_PROFILE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t offset,length;
  const uint8_t *data;
} t_menu_overlay_fragment;
typedef struct {
  uint32_t id,anchor,nor_exit;
  unsigned nor_count;
  const t_menu_overlay_fragment *nor_fragments;
} t_menu_overlay_profile;

const t_menu_overlay_profile *menu_overlay_profile_get(uint32_t id);
void menu_overlay_apply(const t_menu_overlay_profile*,uint8_t*,unsigned,uint32_t,bool);
void menu_overlay_install_nor(const t_menu_overlay_profile*);
#endif
