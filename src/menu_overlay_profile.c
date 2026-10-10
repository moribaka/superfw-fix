#include "menu_overlay_profile.h"
#include "menu_overlay_profiles.h"
#include "menu_overlay.h"
#include "patchengine.h"
#include "menu_layout_offsets.h"
#include "common.h"
#include "ingame.h"
#include "util.h"
#include "settings.h"
#include "save.h"
#include "fonts/font_render.h"
#include "supercard_driver.h"
#include "fatfs/ff.h"

#pragma GCC optimize ("Os")
#define OVERLAY_BASE 0x09000000
extern void load_ingame_menu(uint32_t,unsigned,bool,const char*,const char*,bool,unsigned);

const t_menu_overlay_profile *menu_overlay_profile_get(uint32_t id) {
  for(unsigned i=0;i<ARRAY_SIZE(menu_overlay_profiles);++i)
    if(menu_overlay_profiles[i].id==id)return &menu_overlay_profiles[i];
  return NULL;
}

void menu_overlay_apply(const t_menu_overlay_profile *p,uint8_t *buffer,unsigned size,uint32_t base,bool nor) {
  if (!nor) return; // SDRAM menus must never use TF as working storage.
  const t_menu_overlay_fragment *fragments=p->nor_fragments;
  unsigned count=p->nor_count;
  for(unsigned i=0;i<count;++i)
    payload_apply_rom(buffer,size,base,fragments[i].data,fragments[i].length,fragments[i].offset);

}

void menu_overlay_install_nor(const t_menu_overlay_profile *p) {
  for(unsigned i=0;i<p->nor_count;++i) {
    const t_menu_overlay_fragment *f=&p->nor_fragments[i];
    memcpy32((void*)(GBA_ROM_BASE+f->offset),f->data,f->length);
  }
}

