#include "menu_layout.h"
#include "ingame.h"
#include "menu_layout_data.h"
#include "menu_overlay_profile.h"

#pragma GCC optimize ("Os")

const t_menu_layout *menu_layout_get(uint32_t id) {
  for (unsigned i = 0; i < sizeof(menu_layouts)/sizeof(menu_layouts[0]); ++i)
    if (id && menu_layouts[i].id == id) return &menu_layouts[i];
  return 0;
}

static bool overlap(unsigned a, unsigned n, unsigned b, unsigned m) {
  return a < b + m && b < a + n;
}

bool menu_layout_valid(const t_menu_layout *p, unsigned payload, unsigned font) {
  if(p && p->compact){payload=ingame_menu_compact_full_size;font=MENU_COMPACT_FONT_RESERVE;}
  if (!p || !p->count || p->count > MENU_LAYOUT_MAX_REGIONS ||
      p->borrowed_count > 8 ||
      p->total < payload || p->total != (p->compact?ingame_menu_compact_payload.menu_rsize:ingame_menu_payload.menu_rsize) ||
      p->font > 0x02000000 || font > 0x02000000 - p->font ||
      !font || ((p->anchor | p->font | font) & 3)) return false;
  unsigned end = 0;
  for (unsigned i = 0; i < p->count; ++i) {
    const t_menu_region *r = &p->regions[i];
    if (r->offset != end || !r->length ||
        ((r->offset | r->length | r->address) & 3) ||
        r->address < 0x08000000 || r->address > 0x0A000000 ||
        r->length > 0x0A000000-r->address ||
        r->length > p->total-end ||
        overlap(r->address,r->length,0x08000000+p->font,font)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (overlap(r->address,r->length,p->regions[j].address,p->regions[j].length)) return false;
    end += r->length;
  }
  for(unsigned i=0;i<p->borrowed_count;++i) {
    unsigned a=p->borrowed[i][0],n=p->borrowed[i][1];
    if(!n || ((a|n)&3))return false;
    bool matched=false;
    for(unsigned j=0;j<p->count;++j) {
      const t_menu_region *r=&p->regions[j];
      if(a==r->address && n==r->length && r->offset>=payload+3072)matched=true;
    }
    if(!matched)return false;
  }
  return end == p->total && p->regions[0].address == 0x08000000+p->anchor &&
         p->regions[0].length >= MENU_SCATTER_RESIDENT_SIZE &&
         p->regions[0].offset == 0;
}

bool menu_layout_available(const t_patch *patch, unsigned cheats) {
  const t_menu_layout *p = patch ? menu_layout_get(patch->menu_layout) : 0;
  return !cheats && menu_layout_valid(p,ingame_menu_payload_size,MENU_LAYOUT_FONT_RESERVE);
}
