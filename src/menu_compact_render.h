#ifndef MENU_COMPACT_RENDER_H
#define MENU_COMPACT_RENDER_H
#include <stdint.h>
#include "gbahw.h"

// A 240x80 monochrome atlas uses two 32-pixel rows and one 16-pixel row.
// Four visible actions per page retain the original full-resolution glyphs.
// Only 10 KiB of OBJ VRAM is touched; game BG/affine registers remain untouched.
static inline void compact_pixel(unsigned x,unsigned y,unsigned value) {
  if(x>=SCREEN_WIDTH || y<28 || y>=108)return;
  y-=28;
  unsigned column=x/32;
  unsigned tile=(y/32)*120+column*(y>=64?8:16)+((y%32)/8)*(column==7?2:4)+(x%32)/8;
  unsigned byte=tile*32+(y%8)*4+(x%8)/2;
  volatile uint16_t *p=(volatile uint16_t*)(0x06010000+(byte&~1U));
  unsigned shift=(byte&1)*8+(x&1)*4;
  *p=(*p&~(15U<<shift))|((value?1U:0U)<<shift);
}
static inline void compact_rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned value) {
  for(unsigned row=0;row<h && y+row<SCREEN_HEIGHT;++row)
    for(unsigned col=0;col<w && x+col<SCREEN_WIDTH;++col)compact_pixel(x+col,y+row,value);
}
static inline void compact_clear(void) {
  volatile uint16_t *p=(volatile uint16_t*)0x06010000;
  for(unsigned i=0;i<10*1024/2;++i)p[i]=0;
}
static inline void compact_video_init(void) {
  REG_DISPCNT=0x10C0; // Forced blank, OBJ only, 1D tile mapping.
  REG_BLDCNT=0;
  MEM_PALETTE[0]=0;
  MEM_PALETTE[256+1]=0x7FFF;
  for(unsigned i=0;i<128;++i) {
    MEM_OAM[i*4]=0x0200;
    MEM_OAM[i*4+1]=0;
    MEM_OAM[i*4+2]=0;
    MEM_OAM[i*4+3]=0;
  }
  for(unsigned i=0;i<24;++i) {
    unsigned edge=(i%8)==7;
    unsigned shortrow=i>=16;
    MEM_OAM[i*4]=(i/8)*32+16+(shortrow?(edge?0:0x4000):(edge?0x8000:0));
    MEM_OAM[i*4+1]=(shortrow&&edge?0x4000:0x8000)+(i%8)*32;
    MEM_OAM[i*4+2]=(i/8)*120+(i%8)*(shortrow?8:16);
  }
  compact_clear();
}
void compact_text(const char*,unsigned,unsigned,unsigned,unsigned,unsigned);
#endif
