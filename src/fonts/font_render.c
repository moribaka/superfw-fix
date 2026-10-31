/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "compiler.h"
#include "utf_util.h"
#include "font_embed.h"
#include "hangul.h"

extern void *font_base_addr;

#ifdef COMPACT_INGAME_MENU
#include "menu_compact_render.h"
#endif

// Memory structures that describe character/font data.

#define FLAG_FW16     0x00000001
#define FLAG_COMP     0x80000000

typedef struct {
  uint32_t start_char;   // First unicode char represented in this block
  uint32_t end_char;     // Last unicode char represented in this block
  uint32_t flags;        // Flags
  uint32_t block_off;    // Byte offset to the actual block data
} t_charblock_info;

typedef struct {
  char magic[2];                    // Set to "FO" in ASCII
  uint8_t version;                  // Usually v1
  uint8_t block_count;              // Number of charblks
  uint32_t data_size;               // Total size (with padding)
  t_charblock_info charblks[];
} t_charblock_header;

typedef struct {
  unsigned char_width;         // Width of the glyph
  unsigned spacing_cols;       // Number of spacing columns required after.
  unsigned nchars;             // Number of glyphs to overimpose.
  const uint16_t *data[3];     // Actual rendering data (column data).
} t_char_render_info;

#define MISSING_CHAR    26   // "?" char, should find a better one though
#define CHAR_SPACING     1

// Looks up block info for a character code.
static bool lookup_chptr(uint32_t code, t_char_render_info *chinfo) {
  // Add here any font database pointers as you wish, they are looked up in order.
  const void *font_dblist[] = {
    font_ascii_embedded,
    font_base_addr,
  };

  for (unsigned j = 0; j < sizeof(font_dblist)/sizeof(font_dblist[0]); j++) {
    const t_charblock_header *chdat = (const t_charblock_header*)(font_dblist[j]);

    // Compact sparse glyphs keep full-resolution artwork with a four-byte
    // code/width/spacing header, instead of a separate 16-byte block per glyph.
    if(chdat->magic[0]=='F' && chdat->magic[1]=='C') {
      const uint16_t *entry=(const uint16_t*)((const uint8_t*)chdat+8);
      for(unsigned i=0;i<chdat->block_count;++i,entry+=18) {
        if(entry[0]!=code)continue;
        chinfo->char_width=entry[1]&255;chinfo->spacing_cols=entry[1]>>8;
        chinfo->nchars=1;chinfo->data[0]=entry+2;return true;
      }
      continue;
    }

    // Points to the data area (after all the indices)
    const uint8_t *baseptr = (uint8_t*)&chdat->charblks[chdat->block_count];

    for (unsigned i = 0; i < chdat->block_count; i++) {
      if (code >= chdat->charblks[i].start_char &&
          code <= chdat->charblks[i].end_char) {

        // Get code offset, and pointer to the data region for the block.
        uint32_t code_offset = code - chdat->charblks[i].start_char;
        const uint16_t *chptr = (uint16_t*)&baseptr[chdat->charblks[i].block_off];

        // Fill the render info struct
        if (chdat->charblks[i].flags & FLAG_COMP) {
          unsigned glyphs[3];
          chinfo->char_width = 16;
          chinfo->spacing_cols = 0;
          chinfo->nchars = hangul_glyphs(code_offset, &glyphs[0], &glyphs[1], &glyphs[2]);
          for (unsigned j = 0; j < chinfo->nchars; j++)
            chinfo->data[j] = &chptr[16 * glyphs[j]];
        } else if (chdat->charblks[i].flags & FLAG_FW16) {
          chinfo->char_width = 16;
          chinfo->spacing_cols = 0;   // No spacing for fixed width chars.
          chinfo->nchars = 1;
          chinfo->data[0] = &chptr[16 * code_offset];
        } else {
          // Lookup the second index (contains widths and offsets)
          uint16_t ientry = chptr[code_offset];
          const uint16_t *chdata = &chptr[chdat->charblks[i].end_char - chdat->charblks[i].start_char + 1];

          chinfo->char_width = (ientry >> 13) + 1;
          chinfo->spacing_cols = CHAR_SPACING;
          chinfo->nchars = 1;
          chinfo->data[0] = &chdata[ientry & 0x1FFF];
        }
        return true;
      }
    }
  }
  return false;
}

unsigned font_block_size() {
  const t_charblock_header *chdat = (const t_charblock_header*)(font_base_addr);
  return chdat->data_size;
}

#ifndef NO_SUPERCARD_INIT
#include "menu_font_chars.h"
#include "menu_compact_font_chars.h"

unsigned font_menu_compact_size(unsigned language) {
  if(language>=sizeof(menu_compact_charsets)/sizeof(menu_compact_charsets[0]))language=0;
  return 8+menu_compact_charsets[language].count*36;
}

void font_menu_compact_copy(void *destination,unsigned language) {
  if(language>=sizeof(menu_compact_charsets)/sizeof(menu_compact_charsets[0]))language=0;
  unsigned count=menu_compact_charsets[language].count;
  volatile uint32_t *header=(volatile uint32_t*)destination;
  header[0]=0x00014346|count<<24;header[1]=8+count*36;
  volatile uint16_t *out=(volatile uint16_t*)((uint8_t*)destination+8);
  for(unsigned i=0;i<count;++i) {
    unsigned code=menu_compact_charsets[language].chars[i];t_char_render_info glyph;
    if(!lookup_chptr(code,&glyph))lookup_chptr(MISSING_CHAR,&glyph);
    *out++=code;*out++=glyph.char_width|glyph.spacing_cols<<8;
    for(unsigned col=0;col<16;++col) {
      uint16_t bits=0;
      if(col<glyph.char_width)for(unsigned part=0;part<glyph.nchars;++part)bits|=glyph.data[part][col];
      *out++=bits;
    }
  }
}

unsigned font_menu_subset_size(unsigned language) {
  if (language >= sizeof(menu_charsets) / sizeof(menu_charsets[0]))
    language = 0;
  return 8 + menu_charsets[language].count * 52;
}

// Relocate only the selected menu's glyphs when a full ROM has a small hole.
// ASCII/icons are already embedded in the menu. Preserve widths and compose
// Hangul exactly as the normal renderer does. All cartridge writes are 16/32 bit.
void font_menu_subset_copy(void *destination, unsigned language) {
  if (language >= sizeof(menu_charsets) / sizeof(menu_charsets[0]))
    language = 0;
  unsigned count = menu_charsets[language].count;
  volatile uint32_t *header = (volatile uint32_t*)destination;
  header[0] = 0x00014F46 | count << 24;
  header[1] = 8 + count * 52;
  for (unsigned index = 0; index < count; index++) {
    unsigned code = menu_charsets[language].chars[index];
    t_char_render_info glyph;
    if (!lookup_chptr(code, &glyph))
      lookup_chptr(MISSING_CHAR, &glyph);
    bool fixed = glyph.char_width == 16 && !glyph.spacing_cols;
    header[2 + index * 4] = code;
    header[3 + index * 4] = code;
    header[4 + index * 4] = fixed ? FLAG_FW16 : 0;
    header[5 + index * 4] = index * 36;
    volatile uint16_t *data = (volatile uint16_t*)((uint8_t*)destination + 8 + count * 16 + index * 36);
    for (unsigned column = 0; column < 18; column++)
      data[column] = 0;
    if (!fixed)
      *data++ = (glyph.char_width - 1) << 13;
    for (unsigned column = 0; column < glyph.char_width; column++) {
      uint16_t pixels = 0;
      for (unsigned part = 0; part < glyph.nchars; part++)
        pixels |= glyph.data[part][column];
      data[column] = pixels;
    }
  }
}
#endif

unsigned font_width(const char *s) {
  unsigned pxcnt = 0;
  while (*s) {
    t_char_render_info chinfo;
    uint32_t code = utf8_decode(s);
    if (!lookup_chptr(code, &chinfo))
      lookup_chptr(MISSING_CHAR, &chinfo);

    pxcnt += chinfo.char_width + chinfo.spacing_cols;

    s += utf8_chlen(s);
  }
  return pxcnt;
}

#ifdef COMPACT_INGAME_MENU
// Render the same Unicode glyphs directly into the OBJ canvas. There is no
// automatic framebuffer, separate font format, or byte write to VRAM.
void compact_text(const char *s,unsigned x,unsigned y,unsigned skip,unsigned maxcols,unsigned disabled) {
  unsigned out=0;
  while(*s && out<maxcols && x+out<SCREEN_WIDTH) {
    t_char_render_info glyph;
    uint32_t code=utf8_decode(s);
    if(!lookup_chptr(code,&glyph))lookup_chptr(MISSING_CHAR,&glyph);
    for(unsigned col=0;col<glyph.char_width+glyph.spacing_cols;++col) {
      if(skip){--skip;continue;}
      if(out>=maxcols || x+out>=SCREEN_WIDTH)return;
      uint16_t bits=0;
      if(col<glyph.char_width)
        for(unsigned part=0;part<glyph.nchars;++part)bits|=glyph.data[part][col];
      for(unsigned row=0;row<16 && y+row<SCREEN_HEIGHT;++row)
        if((bits&(1U<<row)) && (!disabled || ((x+out+y+row)&1)==0))compact_pixel(x+out,y+row,1);
      ++out;
    }
    s+=utf8_chlen(s);
  }
}
#endif

unsigned font_width_lcap(const char *s, unsigned max_width) {
  int pxcnt = font_width(s);
  unsigned bcnt = 0;
  while (s[bcnt]) {
    if (pxcnt <= (signed)max_width)
      break;

    t_char_render_info chinfo;
    uint32_t code = utf8_decode(&s[bcnt]);
    if (!lookup_chptr(code, &chinfo))
      lookup_chptr(MISSING_CHAR, &chinfo);

    unsigned chwidth = chinfo.char_width + chinfo.spacing_cols;
    pxcnt -= chwidth;
    bcnt += utf8_chlen(&s[bcnt]);
  }
  return bcnt;
}

unsigned font_width_cap(const char *s, unsigned max_width) {
  unsigned pxcnt = 0, bcnt = 0;
  while (s[bcnt]) {
    t_char_render_info chinfo;
    uint32_t code = utf8_decode(&s[bcnt]);
    if (!lookup_chptr(code, &chinfo))
      lookup_chptr(MISSING_CHAR, &chinfo);

    unsigned chwidth = chinfo.char_width + chinfo.spacing_cols;
    unsigned newwidth = pxcnt + chwidth;
    if (newwidth > max_width)
      break;
    pxcnt = newwidth;
    bcnt += utf8_chlen(&s[bcnt]);
  }
  return bcnt;
}

unsigned font_width_cap_space(const char *s, unsigned max_width, unsigned *outwidth) {
  unsigned pxcnt = 0, bcnt = 0, max_cnt = 0, max_w = 0;
  while (s[bcnt]) {
    if (s[bcnt] == ' ') {
      max_cnt = bcnt;
      max_w = pxcnt;
    }

    t_char_render_info chinfo;
    uint32_t code = utf8_decode(&s[bcnt]);
    if (!lookup_chptr(code, &chinfo))
      lookup_chptr(MISSING_CHAR, &chinfo);

    unsigned chwidth = chinfo.char_width + chinfo.spacing_cols;
    unsigned newwidth = pxcnt + chwidth;
    if (newwidth > max_width) {
      *outwidth = max_w;
      return max_cnt;
    }
    pxcnt = newwidth;
    bcnt += utf8_chlen(&s[bcnt]);
  }
  *outwidth = pxcnt;
  return bcnt;
}

static inline void vram_write(uint8_t *buffer, uint8_t value) {
  uintptr_t dst = (uintptr_t)buffer;
  if (dst & 1) {
    volatile uint16_t *b16 = (uint16_t*)&buffer[-1];
    *b16 = (value << 8) | (*b16 & 0xFF);
  } else {
    volatile uint16_t *b16 = (uint16_t*)buffer;
    *b16 = value | (*b16 & 0xFF00);
  }
}

// Special GBA routine: handles VRAM byte writes correctly
// Renders some text in a framebuffer (8bit indexed color)
void draw_text_idx8_bus16_range(const char *s, uint8_t *buffer, unsigned skip, unsigned maxcols, unsigned pitch, uint8_t color) {
  uint8_t *buf_max = &buffer[maxcols];
  while (*s) {
    t_char_render_info chinfo;
    uint32_t code = utf8_decode(s);
    if (!lookup_chptr(code, &chinfo))
      lookup_chptr(MISSING_CHAR, &chinfo);

    unsigned ncols = chinfo.char_width;
    unsigned totalcols = ncols + chinfo.spacing_cols;

    if (skip && skip >= totalcols) {
      skip -= totalcols;
    } else {
      for (unsigned i = 0; i < ncols; i++) {
        if (buffer >= buf_max)
          return;
        if (skip) {
          skip--;
          continue;
        }

        uint16_t ch = chinfo.data[0][i];
        for (unsigned c = 1; c < chinfo.nchars; c++)
          ch |= chinfo.data[c][i];

        #pragma GCC unroll 16
        for (unsigned j = 0; j < 16; j++)
          if (ch & (1 << j))
            vram_write(&buffer[pitch * j], color);
        buffer++;
      }

      // Add optional columns of empty space
      unsigned space_skip = chinfo.spacing_cols >= skip ? chinfo.spacing_cols - skip :
                                                          chinfo.spacing_cols;
      buffer += space_skip;
    }

    s += utf8_chlen(s);
  }
}

ARM_CODE IWRAM_CODE NOINLINE
void draw_text_idx8_bus16(const char *s, uint8_t *buffer, unsigned pitch, uint8_t color) {
  while (*s) {
    t_char_render_info chinfo;
    uint32_t code = utf8_decode(s);
    if (!lookup_chptr(code, &chinfo))
      lookup_chptr(MISSING_CHAR, &chinfo);

    unsigned ncols = chinfo.char_width;
    for (unsigned i = 0; i < ncols; i++) {
      uint16_t ch = chinfo.data[0][i];
      for (unsigned c = 1; c < chinfo.nchars; c++)
        ch |= chinfo.data[c][i];

      #pragma GCC unroll 16
      for (unsigned j = 0; j < 16; j++)
        if (ch & (1 << j))
          vram_write(&buffer[pitch * j + i], color);
    }

    // Add optional columns of empty space
    buffer += ncols + chinfo.spacing_cols;

    s += utf8_chlen(s);
  }
}

