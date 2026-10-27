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
#include <stdlib.h>
#include <string.h>

#include "gbahw.h"
#include "compiler.h"
#include "save.h"
#include "patchengine.h"
#include "webpatch.h"
#include "romcompat.h"
#include "rtc_state.h"
#include "rtc_custom.h"
#include "settings.h"
#include "ingame.h"
#include "menu_layout.h"
#include "menu_compact_config.h"
#include "menu_layout_offsets.h"
#include "menu_overlay_profile.h"
#include "fonts/font_render.h"
#include "supercard_driver.h"
#include "fatfs/ff.h"
#include "directsave.h"
#include "common.h"
#include "cheats.h"
#include "sfc.h"
#include "util.h"
#include "flash_mgr.h"
#include "flash.h"

// Here we have the ROM loading routines.

#define LOAD_BS     (8*1024)     // Load in 8KB chunks

#define GBA_ROM_ADDR                  ((uint8_t *)0x08000000)
#define GBA_ROM_ADDR16(addr, value)   *((volatile uint16_t *)(0x08000000 + addr)) = (value)
#define GBA_ROM_ADDR32(addr, value)   *((volatile uint32_t *)(0x08000000 + addr)) = (value)

// The firmware block (#0) is mapped to the last 4MiB block, plus its offset.
#define FLASH_DIRSAV_PAYLOAD_W0       (0x0A000000 - NOR_BLOCK_SIZE + 0x00190000)
#define FLASH_IGM_TRAMPOLINE_W0       (0x0A000000 - NOR_BLOCK_SIZE + 0x00198000)

#define ING_PALETTE_BASE    240

extern bool slowsd;
extern const uint8_t ingame_nor_scatter_payload[];
extern const uint32_t ingame_nor_scatter_payload_size;

bool validate_gba_header(const uint8_t *header) {
  return romcompat_gba_header_errors(header, 192) == 0;
}

bool validate_gb_header(const uint8_t *header) {
  // Check that the checksum is OK:
  const t_gbheader* hdr = (t_gbheader*)header;
  uint8_t checksum = 0;
  for (unsigned i = 0x34; i <= 0x4C; i++)
    checksum = checksum - header[i] - 1;
  if (checksum != hdr->checksum)
    return false;

  const uint32_t *logo32 = (uint32_t*)hdr->logo_data;
  uint32_t logocheck = 0;
  for (unsigned i = 0; i < 12; i++)
    logocheck ^= logo32[i];
  if (logocheck != 0x83e1df3b)
    return false;

  return true;
}

void fix_gba_header(volatile uint16_t *header) {
  romcompat_fix_gba_header(header, 192);
}

static void copy_menu_image(uint32_t base,const t_menu_layout *layout,bool compact) {
  unsigned payload_size=compact?ingame_menu_compact_full_size:ingame_menu_payload_size;
#ifdef SUPERCHIS_IO
  const uint8_t *body=(const uint8_t*)(compact?0x09F10000:0x09F00000);
  if (!layout) {
    memmove32((void*)(base+MENU_SCATTER_RESIDENT_SIZE),body,
              payload_size-MENU_SCATTER_RESIDENT_SIZE);
    return;
  }
  for(unsigned i=0;i<layout->count;++i) {
    const t_menu_region *r=&layout->regions[i];
    unsigned n=r->offset<payload_size?payload_size-r->offset:0;
    if(n>r->length)n=r->length;
    if(n && r->offset>=MENU_SCATTER_RESIDENT_SIZE)
      memcpy32((void*)r->address,body+r->offset-MENU_SCATTER_RESIDENT_SIZE,n);
  }
#else
  memcpy32((void*)base,&ingame_menu_payload,ingame_menu_payload_size);
#endif
}

static uint32_t menu_language_work[MENU_LANGUAGE_RESERVE/4];

// Loads the in-game menu at the desired address and size (returns success).
// addr must be 4 byte aligned.
static void load_ingame_menu_layout(
  uint32_t base_addr, uint32_t total_size, bool useds,
  const char* savefn, const char* statefn,
  bool rtc_patches, unsigned cheats, const t_menu_layout *layout
) {
  const bool compact = layout ? layout->compact : total_size < ingame_menu_payload.menu_rsize + (cheats?font_block_size():font_menu_subset_size(lang_id)) + cheats;
  const t_igmenu *image = compact ? &ingame_menu_compact_payload : &ingame_menu_payload;
  const unsigned payload_size=compact?ingame_menu_compact_full_size:ingame_menu_payload_size;
  const unsigned menu_size = image->menu_rsize;
  const bool compact_fonts = layout || (!cheats && total_size < menu_size + font_block_size());
  const unsigned fontsz = compact_fonts ? (compact?font_menu_compact_size(lang_id):font_menu_subset_size(lang_id)) : font_block_size();

  set_supercard_mode(MAPPED_SDRAM, true, false);
  if(compact) {
    const uint32_t *table=(const uint32_t*)0x09F20000;
    unsigned language=lang_id<14?lang_id:0,offset=table[language*2],size=table[language*2+1];
    memset32(menu_language_work,0,sizeof(menu_language_work));
    memcpy32(menu_language_work,(const void*)(0x09F20000+offset),size);
  }

  // Copy the font pack first, using memmove to handle collisions properly.
  // TODO: Allow partial font copying, to reduce memory usage (ie. in 32MiB ROMs)
  uint8_t *ptr = (uint8_t*)base_addr;
  uint8_t *font_ptr = (uint8_t*)ROM_FONTBASE_U8;
  uint8_t *font_dest = layout ? (uint8_t*)(GBA_ROM_BASE+layout->font) : &ptr[menu_size];
  // Choose the move order from the actual source/destination spans. The
  // body source (31 MiB) and font source (14.5 MiB) are far enough apart
  // that these two compact destination spans cannot form a copy cycle.
  unsigned body_start=base_addr+MENU_SCATTER_RESIDENT_SIZE;
  unsigned body_end=base_addr+payload_size;
  bool font_first=layout || (body_start<(uint32_t)font_ptr+font_block_size()+cheats &&
                            (uint32_t)font_ptr<body_end);
  if(!font_first)copy_menu_image(base_addr,layout,compact);
  if (compact_fonts) {
    if(compact)font_menu_compact_copy((void*)ROM_SCRATCH_U8,lang_id);
    else font_menu_subset_copy((void*)ROM_SCRATCH_U8, lang_id);
    memmove32(font_dest,(void*)ROM_SCRATCH_U8,fontsz);
  } else memmove32(font_dest,font_ptr,fontsz+cheats);
  if(font_first)copy_menu_image(base_addr,layout,compact);
#ifdef SUPERCHIS_IO
  memcpy32((void*)base_addr,image,MENU_SCATTER_RESIDENT_SIZE);
  if(compact)memcpy32((void*)(base_addr+MENU_COMPACT_LANGUAGE_BLOCK_OFFSET),menu_language_work,sizeof(menu_language_work));
#endif
  t_igmenu *igm=(t_igmenu*)base_addr;
  if(layout) {
    uint32_t *map=(uint32_t*)(base_addr+MENU_SCATTER_LAYOUT_OFFSET);
    map[0]=layout->count;map[1]=layout->total;
    memcpy32(map+2,layout->regions,sizeof(layout->regions));
    map=(uint32_t*)(base_addr+MENU_SCATTER_BORROWED_OFFSET);
    map[0]=layout->borrowed_count;
    memcpy32(map+1,layout->borrowed,sizeof(layout->borrowed));
    for(unsigned i=0;i<layout->borrowed_count;++i)
      memset32((void*)layout->borrowed[i][0],layout->borrowed[i][2],layout->borrowed[i][1]);
  }

  // Decode the start instruction, calculate branch target.
  uint16_t hotk = hotkey_list[hotkey_combo].mask;
  set_abort_lr(hotk << 16, cheats && (*(const uint32_t*)(ptr+menu_size+fontsz) & CHEAT_ENABLED_FLAG));

  // Patch the menu header to provide necessary data.
  igm->drv_issdhc = sc_issdhc();              // SD driver data (no init happens)
  igm->drv_rca = sc_rca();
  igm->menu_hotkey = hotk;                    // Configured hotkey
  igm->menu_lang = lang_id;                   // Use the current lang id
  igm->menu_use_directsave = useds;           // DirectSave in use
  igm->menu_anim_speed = anim_speed;          // Menu animation speed
  igm->menu_font_base = (uint32_t)font_dest;// Base addr where the fonts live
  igm->menu_cheats_base = cheats ? base_addr + menu_size + fontsz : 0;
  igm->scratch_space_base = layout ? 0 : base_addr + menu_size + fontsz + cheats;
  igm->scratch_space_size = layout ? 0 : total_size - (menu_size + fontsz + cheats);
  igm->menu_has_rtc_support = rtc_patches;    // Using RTC patches
  igm->savefile_backups = backup_sram_default;// Backup count
  for (unsigned i = 0; i < sizeof(igm->menu_palette) / sizeof(igm->menu_palette[0]); i++)
    igm->menu_palette[i] = MEM_PALETTE[ING_PALETTE_BASE + i];

  // Calculate the basename, so we can produce proper sav/backup files
  // Only used if saving is enabled and DirSav is disabled.
  if (savefn && !useds) {
    char save_basename[MAX_FN_LEN];
    strcpy(save_basename, savefn);
    replace_extension(save_basename, "");
    memcpy32(igm->savefile_pattern, save_basename, sizeof(igm->savefile_pattern));
  }
  else
    memset32(igm->savefile_pattern, 0, sizeof(igm->savefile_pattern));

  memcpy32(igm->statefile_pattern, statefn, sizeof(igm->statefile_pattern));
  memset32(igm->rtcfile_path, 0, sizeof(igm->rtcfile_path));

  set_supercard_mode(MAPPED_SDRAM, true, true);
}

void load_ingame_menu(uint32_t base,unsigned size,bool ds,const char *save,const char *state,bool rtc,unsigned cheats) {
  load_ingame_menu_layout(base,size,ds,save,state,rtc,cheats,NULL);
}

// Copy each original ROM byte once, except reviewed occupied menu destinations.
static void copy_scatter_rom(uint8_t *dest,const uint8_t *source,unsigned size,const t_menu_layout *p) {
  unsigned start=0;
  while(start<size) {
    unsigned next=size,skip=start;
    for(unsigned i=0;i<=p->count;++i) {
      uint32_t a=i==p->count?GBA_ROM_BASE+p->font:p->regions[i].address;
      unsigned n=i==p->count?(p->compact?MENU_COMPACT_FONT_RESERVE:MENU_LAYOUT_FONT_RESERVE):p->regions[i].length;
      uint32_t at=(uint32_t)(dest+start);
      if(a<=at && at<a+n) {unsigned end=a+n-(uint32_t)dest;if(end>skip)skip=end;}
      else if(a>at && a-(uint32_t)dest<next)next=a-(uint32_t)dest;
    }
    if(skip>start) {start=MIN(skip,size);continue;}
    if(next>start) {
      if(use_slowld) {
        // The legacy copier always transfers complete 32-byte blocks. A
        // scatter edge must not overwrite its next reservation or wrap at
        // 32 MiB and corrupt the ROM header.
        unsigned n=next-start,bulk=n&~31U;
        if(bulk)rom_copy_write16(dest+start,source+start,bulk);
        volatile uint16_t *to=(volatile uint16_t*)(dest+start+bulk);
        const uint32_t *from=(const uint32_t*)(source+start+bulk);
        for(unsigned tail=bulk;tail<n;tail+=4) {
          uint32_t word=*from++;
          *to++=word;*to++=word>>16;
        }
      }
      else dma_memcpy32(dest+start,source+start,(next-start)/4);
    }
    start=next;
  }
}

void load_directsave_config(const t_dirsave_info *dsinfo) {
  t_dirsave_config cfg = {
    .magic = DIRSAV_CFG_MAGIC,
    .checksum = 0,
    .nrandom = 0xdeadbeef ^ systime(),
    .memory_size = dsinfo->save_size,
    .base_sector = dsinfo->sector_lba,
    .drv_issdhc = sc_issdhc(),
    .drv_rca = sc_rca(),
    .sd_mutex = 0
  };
  const uint32_t *cfg32 = (uint32_t*)(&cfg);
  uint32_t checksum = 0;
  for (unsigned i = 0; i < sizeof(cfg) / 4; i++)
    checksum ^= *cfg32++;
  cfg.checksum = checksum;

  write_sram_buffer((uint8_t*)(&cfg), 64*1024 - sizeof(cfg), sizeof(cfg));
}

void load_rtcclock_data(const t_rtc_info *rtcinfo) {
  set_undef_lrsp(rtcinfo->timestamp, rtc_speed_pack(rtcinfo->ts_step));
}

// Loads ROM header from disk for inspection.
unsigned preload_gba_rom(const char *fn, uint32_t fs, t_rom_header *romh) {
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return ERR_LOAD_BADROM;

  UINT rdbytes;
  bool err = (FR_OK != f_read(&fd, romh, sizeof(*romh), &rdbytes) || rdbytes != sizeof(*romh));

  f_close(&fd);
  return err ? ERR_LOAD_BADROM : 0;
}

__attribute__((noipa))
unsigned load_gba_rom(
  const char *fn, uint32_t fs,
  const char *savefn,
  const t_patch *ptch,
  const t_dirsave_info *dsinfo,
  bool ingame_menu,
  const t_rtc_info *rtcinfo,
  unsigned cheats,
  progress_fn progress
) {

  bool use_rtc_patches = rtcinfo != NULL;
  char rtc_path[MAX_FN_LEN] = {0};
  rtc_state_path(fn, rtc_path);
  if (!rtc_state_prepare_pending(use_rtc_patches ? rtc_path : NULL))
    return ERR_LOAD_BADROM;

  // Determine how much ROM space we need for the IGM and DirSav payloads
  unsigned igm_reqsz = ingame_menu_payload.menu_rsize + font_block_size() + cheats;
  // Round it up, reserve ~1KB after the ROM for patches.
  // 32MiB games cannot generate patches beyond the end.
  const unsigned romrsize = MAX(patch_rom_size(fs, ptch, use_rtc_patches),
                               ROUND_UP2(fs, 1024) + (fs < MAX_GBA_ROM_SIZE ? 1024 : 0));
  if (ingame_menu && !cheats && romrsize + igm_reqsz + (dsinfo ? DIRSAVE_REQ_SPACE : 0) > MAX_GBA_ROM_SIZE &&
      ptch && ptch->hole_size < igm_reqsz + (dsinfo ? DIRSAVE_REQ_SPACE : 0))
    igm_reqsz = ingame_menu_payload.menu_rsize + font_menu_subset_size(lang_id);
  if (ingame_menu && romrsize + igm_reqsz + (dsinfo?DIRSAVE_REQ_SPACE:0) > MAX_GBA_ROM_SIZE &&
      ptch && ptch->hole_size < igm_reqsz + (dsinfo?DIRSAVE_REQ_SPACE:0))
    igm_reqsz = ingame_menu_compact_payload.menu_rsize + (cheats?font_block_size():font_menu_compact_size(lang_id)) + cheats;
  // Required size for these payloads. We should always have enough, since menu checks it.
  const unsigned req_size = (ingame_menu ? igm_reqsz : 0) + (dsinfo ? DIRSAVE_REQ_SPACE : 0);

  const t_menu_layout *layout=NULL;
  uint32_t igm_addr, igm_space, ds_addr;
  const t_menu_layout *reviewed=ptch?menu_layout_get(ptch->menu_layout):NULL;
  if(ingame_menu && !dsinfo && reviewed && reviewed->compact && menu_layout_available(ptch,cheats)) {
    layout=reviewed;
    ds_addr=layout->anchor;
    igm_addr=ds_addr;
    igm_space=layout->total+MENU_COMPACT_FONT_RESERVE;
  }
  else if (romrsize + req_size <= MAX_GBA_ROM_SIZE) {
    // Allocate the DirSav payload first
    ds_addr = romrsize;
    // Now the IGM
    igm_addr = ds_addr + (dsinfo ? DIRSAVE_REQ_SPACE : 0);
    // Calculate the total space for the IGM to use
    igm_space = MAX_GBA_ROM_SIZE - igm_addr;
  }
  else {
    // Cannot append it at the end, it's too big. Check if we have a hole.
    if (!ptch || ptch->hole_size < req_size) {
      if (!ingame_menu || dsinfo || !menu_layout_available(ptch,cheats)) return ERR_NO_PAYLOAD_SPACE;
      layout=menu_layout_get(ptch->menu_layout);
    }

    ds_addr = layout ? layout->anchor : ptch->hole_addr;
    igm_addr = ds_addr + (dsinfo ? DIRSAVE_REQ_SPACE : 0);
    igm_space = layout ? layout->total + (layout->compact?MENU_COMPACT_FONT_RESERVE:MENU_LAYOUT_FONT_RESERVE) : ptch->hole_size - (dsinfo ? DIRSAVE_REQ_SPACE : 0);
  }
  // Round it down to a KB boundary
  igm_space &= ~1023;

  // Calculate the "hole" limits
  uint32_t gap_start = ds_addr;
  uint32_t gap_end = layout ? fs : igm_addr + igm_space;
  if(layout)gap_start=fs;

  // Get aboslute addresses
  ds_addr += GBA_ROM_BASE;
  igm_addr += GBA_ROM_BASE;

  // Install the menu before loading the ROM, otherwise we overwrite relevant assets.
  if (ingame_menu) {
    char sfn[MAX_FN_LEN];
    savestate_filename_calc(fn, sfn);
    load_ingame_menu_layout(igm_addr, igm_space, dsinfo, savefn, sfn, use_rtc_patches, cheats,layout);
    if (use_rtc_patches) {
      set_supercard_mode(MAPPED_SDRAM, true, false);
      memcpy32(((t_igmenu*)igm_addr)->rtcfile_path, rtc_path, sizeof(rtc_path));
      set_supercard_mode(MAPPED_SDRAM, true, true);
    }
  }

  // Proceed to load the ROM
  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return ERR_LOAD_BADROM;

  // Calculate progress bar steps, carefully consider the gap (if any).
  const uint32_t first_end = MIN(gap_start, fs);
  const uint32_t load_steps = (first_end + LOAD_BS - 1) / LOAD_BS +
                             (fs > gap_end ? (fs - gap_end + LOAD_BS - 1) / LOAD_BS : 0);
  uint32_t steps = 0;

  // Honor fast loading (switch mirror if appropriate)
  slowsd = use_slowld;

  uint8_t *ptr = (uint8_t*)(GBA_ROM_ADDR);
  for (uint32_t offset = 0; offset < first_end; offset += LOAD_BS, steps++) {
    if (progress && (steps & (31)) == 0)
      progress(steps, load_steps);

    unsigned toread = MIN(LOAD_BS, first_end - offset);
    UINT rdbytes;
    uint32_t tmp[LOAD_BS/4];
    if (toread & 3)
      tmp[toread / 4] = 0xFFFFFFFF;
    if (FR_OK != f_read(&fd, tmp, toread, &rdbytes) || rdbytes != toread) {
      slowsd = true;
      f_close(&fd);
      return ERR_LOAD_BADROM;
    }

    set_supercard_mode(MAPPED_SDRAM, true, false);
    if (layout)copy_scatter_rom(&ptr[offset],(const uint8_t*)tmp,ROUND_UP2(toread,4),layout);
    else if (use_slowld)
      rom_copy_write16(&ptr[offset], tmp, ROUND_UP2(toread, 4));
    else
      dma_memcpy32(&ptr[offset], tmp, (toread + 3)/4);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }
  // Skip over the gap
  if (gap_end < fs && FR_OK != f_lseek(&fd, gap_end)) {
    slowsd = true;
    f_close(&fd);
    return ERR_LOAD_BADROM;
  }
  for (uint32_t offset = gap_end; offset < fs; offset += LOAD_BS, steps++) {
    if (progress && (steps & (31)) == 0)
      progress(steps, load_steps);

    unsigned toread = MIN(LOAD_BS, fs - offset);
    UINT rdbytes;
    uint32_t tmp[LOAD_BS/4];
    if (toread & 3)
      tmp[toread / 4] = 0xFFFFFFFF;
    if (FR_OK != f_read(&fd, tmp, toread, &rdbytes) || rdbytes != toread) {
      slowsd = true;
      f_close(&fd);
      return ERR_LOAD_BADROM;
    }

    set_supercard_mode(MAPPED_SDRAM, true, false);
    if (use_slowld)
      rom_copy_write16(&ptr[offset], tmp, ROUND_UP2(toread, 4));
    else
      dma_memcpy32(&ptr[offset], tmp, (toread + 3)/4);
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }
  slowsd = true;

  // Close the file, not super necessary really :P
  f_close(&fd);

  if (fs < gap_start) {
    set_supercard_mode(MAPPED_SDRAM, true, false);
    memset32(ptr + ROUND_UP2(fs, 4), 0xFFFFFFFF, gap_start - ROUND_UP2(fs, 4));
    set_supercard_mode(MAPPED_SDRAM, true, true);
  }

  // Proceed to patch the ROM
  if (ptch && ptch->web_edits) {
    t_webpatch_reader edits;
    if (!webpatch_open(&edits, fn, ptch)) return ERR_LOAD_BADROM;
    bool applied = webpatch_process(&edits, ptr, MAX_GBA_ROM_SIZE, 0);
    webpatch_close(&edits);
    if (!applied) return ERR_LOAD_BADROM;
  }
  set_supercard_mode(MAPPED_SDRAM, true, false);

  // Load/Patch the DirectSave payload if necessary.
  if (dsinfo)
    payload_apply_rom(GBA_ROM_ADDR, MAX_GBA_ROM_SIZE, GBA_ROM_BASE, directsave_payload, directsave_payload_size, ds_addr);

  // Actually apply patches
  if (ptch) {
    if (!patch_apply_rom(GBA_ROM_ADDR, MAX_GBA_ROM_SIZE, 0, true, ptch, use_rtc_patches,
                         ingame_menu ? igm_addr : 0, dsinfo ? ds_addr : 0, false))
      return ERR_LOAD_MENU;
    if (rtcinfo)
      load_rtcclock_data(rtcinfo);
  }

  // Fix header checksum unconditionally (just in case we boot to BIOS).
  fix_gba_header((uint16_t*)GBA_ROM_ADDR);

  // Go ahead and load config in SRAM, if applicable.
  if (dsinfo)
    load_directsave_config(dsinfo);

  if (progress)
    progress(1, 1);

  // Set the ROM into read only mode, disable SD card reader as well. Maps SRAM bank 0.
  set_supercard_mode(MAPPED_SDRAM, false, false);

  REG_WAITCNT = 0x4000;
  launch_reset(boot_bios_splash, use_fastew);

  return 0;
}

// Flashes a game to NOR patching it as necessary. This includes DirSav as well as IGM.
NOINLINE
unsigned flash_gba_nor(
  const char *fn, uint32_t fs,
  const t_rom_header *rom_header,
  const t_patch *ptch,
  bool dirsaving,
  bool ingame_menu,
  bool rtc_patches,
  const uint8_t *blkmap,
  progress_fn progress,
  uint8_t *scratch, unsigned ssize
) {
  if(patch_has_sdram_only_ops(ptch))return ERR_FLASH_OP;

  if (!flashinfo.size || !flashinfo.blksize || !flashinfo.blkcount || !flashinfo.blkwrite || !flashinfo.blksize)
    return ERR_FLASH_OP;
  if (ingame_menu && !patch_igm_supported(ptch, true))
    return ERR_LOAD_MENU;

  // Determine if the DirSav payload must be flashed in a ROM gap or not.
  const t_menu_layout *nor_layout=ingame_menu?menu_layout_get(ptch->menu_layout):NULL;
  const t_menu_overlay_profile *nor_overlay=ingame_menu?menu_overlay_profile_get(ptch->menu_layout):NULL;
  const unsigned image_size = patch_rom_size(fs, ptch, rtc_patches);
  const bool flashmap0 = image_size <= MAX_GBA_ROM_SIZE - NOR_BLOCK_SIZE;
  uint32_t ds_flashoffset  = flashmap0 ? 0 : ptch->hole_addr;
  uint32_t igm_flashoffset = nor_overlay ? nor_overlay->anchor : nor_layout ? nor_layout->anchor : flashmap0 ? 0 : ptch->hole_addr + DIRSAVE_REQ_SPACE;

  // Calculate where the DirSav / IGM is (flash mapped block0 or flashed on top of the game).
  uint32_t dsaddr  =  ds_flashoffset ? 0x08000000 +  ds_flashoffset : FLASH_DIRSAV_PAYLOAD_W0;
  uint32_t igmaddr = igm_flashoffset ? 0x08000000 + igm_flashoffset : FLASH_IGM_TRAMPOLINE_W0;

  FIL fd;
  FRESULT res = f_open(&fd, fn, FA_READ);
  if (res != FR_OK)
    return ERR_LOAD_BADROM;

  t_webpatch_reader edits;
  if (!webpatch_open(&edits, fn, ptch)) {
    f_close(&fd);
    return ERR_LOAD_BADROM;
  }

  // Map the game to the base 32MiB address space.
  set_superchis_normap(blkmap);

  t_flash_erase_state erst;
  for (uint32_t bigoff = 0; bigoff < image_size; bigoff += ssize) {
    // Start clearing the flash block we will be writing to!
    flash_erase_fsm_start(&erst, GBA_ROM_BASE_WS1 + bigoff, flashinfo.blksize, ssize / flashinfo.blksize);

    if (image_size > fs && bigoff + ssize > fs) {
      unsigned start = fs > bigoff ? ROUND_UP2(fs - bigoff, 4) : 0;
      set_supercard_mode(MAPPED_SDRAM, true, false);
      memset32(scratch + start, 0xFFFFFFFF, MIN(ssize, image_size - bigoff) - start);
      set_supercard_mode(MAPPED_SDRAM, true, true);
    }

    // Load the ROM in chunks to the scratch area. So that we can mange it.
    for (uint32_t offset = 0; offset < ssize && offset + bigoff < fs; offset += LOAD_BS) {
      uint32_t absoff = offset + bigoff;
      if ((absoff & (128*1024-1)) == 0) {
        if (progress)
          progress((bigoff + offset / 4) >> 8, image_size >> 8);

        flash_erase_fsm_step(&erst);
      }

      unsigned toread = MIN(LOAD_BS, fs - absoff);
      UINT rdbytes;
      uint32_t tmp[LOAD_BS/4];
      if (toread & 3)
        tmp[toread / 4] = 0xFFFFFFFF;
      if (FR_OK != f_read(&fd, tmp, toread, &rdbytes) || rdbytes != toread) {
        webpatch_close(&edits);
        f_close(&fd);
        reset_superchis_normap();
        return ERR_LOAD_BADROM;
      }

      dma_memcpy32(&scratch[offset], tmp, (toread + 3)/4);
    }

    if (!webpatch_process(&edits, scratch, ssize, bigoff)) {
      webpatch_close(&edits);
      f_close(&fd);
      reset_superchis_normap();
      return ERR_LOAD_BADROM;
    }

    if (ingame_menu && bigoff && !(ptch->irq_flags & IRQPATCH_ENTRY_CHECKS_SCANNED)) {
      uint32_t branch = 0xEA000000 | ((igmaddr - GBA_ROM_BASE - 8) >> 2);
      romcompat_apply_legacy_entry_checks(scratch, ssize, bigoff, rom_header->start_branch, branch);
    }

    if(nor_overlay)menu_overlay_apply(nor_overlay,scratch,ssize,bigoff,true);
    if(nor_layout) {
      payload_apply_rom(scratch,ssize,bigoff,ingame_nor_scatter_payload,ingame_nor_scatter_payload_size,igm_flashoffset);
      payload_apply_rom(scratch,ssize,bigoff,(const uint8_t*)&nor_layout->id,4,igm_flashoffset+8);
    }

    // Patch ROM, don't need WAITCNT patches
    if (!patch_apply_rom(scratch, ssize, bigoff, false, ptch, rtc_patches,
                         ingame_menu ? igmaddr : 0, dirsaving ? dsaddr : 0, true)) {
      webpatch_close(&edits);
      f_close(&fd);
      reset_superchis_normap();
      return ERR_FLASH_OP;
    }

    // Copy (partial) DirSav / IGM trampoline payloads if necessary
    if (dirsaving && ds_flashoffset)
      payload_apply_rom(scratch, ssize, bigoff, directsave_payload, directsave_payload_size, ds_flashoffset);

    if (ingame_menu && igm_flashoffset && !nor_layout && !nor_overlay)
      payload_apply_rom(scratch, ssize, bigoff, ingame_trampoline_payload, ingame_trampoline_payload_size, igm_flashoffset);

    if (!bigoff)
      fix_gba_header((uint16_t*)scratch);

    // Wait until erasing is complete (if it didn't complete in the meantime)
    while (1) {
      int r = flash_erase_fsm_step(&erst);
      if (r > 0)
        break;
      if (r < 0) {
        webpatch_close(&edits);
        f_close(&fd);
        reset_superchis_normap();
        return ERR_FLASH_OP;
      }
    }

    // Write flash blocks.
    for (uint32_t offset = 0; offset < ssize && offset + bigoff < image_size; offset += flashinfo.blksize) {
      uint32_t absoff = offset + bigoff;
      uint32_t flashaddr = GBA_ROM_BASE_WS1 + absoff;
      if (progress && (absoff & (128*1024-1)) == 0)
        progress((bigoff + ssize / 4 + offset * 3/4) >> 8, image_size >> 8);

      unsigned toflash = MIN(flashinfo.blksize, image_size - absoff);
      bool wr_ok = flash_program_buffered(flashaddr, &scratch[offset], toflash, flashinfo.blkwrite);

      // Check the written block if so configured
      if (wr_ok && use_verify_nor)
        wr_ok = flash_verify(flashaddr, &scratch[offset], toflash);

      if (!wr_ok) {
        webpatch_close(&edits);
        f_close(&fd);
        reset_superchis_normap();
        return ERR_FLASH_OP;
      }
    }
  }

  webpatch_close(&edits);
  reset_superchis_normap();
  return 0;
}

__attribute__((noipa))
unsigned launch_gba_nor(
  const char *romfn, const char *savefn,
  const uint8_t *normap, unsigned blkcnts,
  const t_dirsave_info *dsinfo,
  const t_rtc_info *rtcinfo,
  bool ingame_menu,
  unsigned cheats
) {

  bool use_rtc_patches = rtcinfo != NULL;
  char rtc_path[MAX_FN_LEN] = {0};
  rtc_state_path(romfn, rtc_path);
  if (!rtc_state_prepare_pending(use_rtc_patches ? rtc_path : NULL))
    return ERR_LOAD_BADROM;

  const t_menu_layout *nor_layout=NULL;
  const t_menu_overlay_profile *nor_overlay=NULL;
  // Check the actual mapped game once on every NOR launch, with or without IGM.
  // Existing NOR is read-only here: a bad header requires rewriting the game.
  set_superchis_normap(normap);
  set_supercard_mode(MAPPED_FIRMWARE,false,false);
  if (!validate_gba_header(GBA_ROM_ADDR)) {
    set_supercard_mode(MAPPED_SDRAM,true,true);
    reset_superchis_normap();
    return ERR_LOAD_BADROM;
  }
  if(ingame_menu) {
    uint32_t branch=*(const uint32_t*)GBA_ROM_BASE;
    uint32_t target=GBA_ROM_BASE+8+((branch&0xFFFFFF)<<2);
    if((branch>>24)==0xEA && target>=GBA_ROM_BASE && target<0x0A000000-12 &&
       *(const uint32_t*)(target+4)==0x32574653) {
      unsigned id=*(const uint32_t*)(target+8);
      nor_layout=menu_layout_get(id);
      nor_overlay=menu_overlay_profile_get(id);
      if((!nor_layout&&!nor_overlay) || target!=GBA_ROM_BASE+(nor_layout?nor_layout->anchor:nor_overlay->anchor)) {
        set_supercard_mode(MAPPED_SDRAM,true,true);reset_superchis_normap();return ERR_LOAD_MENU;
      }
    }
  }
  set_supercard_mode(MAPPED_SDRAM,true,true);

  // Now the IGM: it sits along in the SDRAM (at 0x0 offset)
  uint32_t igm_addr = GBA_ROM_BASE+((nor_layout||nor_overlay)?16*1024*1024:0);
  // IGM can use as much SDRAM as it needs, use 16MiB for now
  uint32_t igm_space = 16*1024*1024;

  // Install the menu before loading the ROM, otherwise we overwrite relevant assets.
  if (ingame_menu) {
    char sfn[MAX_FN_LEN];
    savestate_filename_calc(romfn, sfn);
    load_ingame_menu(igm_addr, igm_space, dsinfo, savefn, sfn, use_rtc_patches, cheats);
    if(nor_overlay) {
      set_supercard_mode(MAPPED_SDRAM,true,false);
      menu_overlay_install_nor(nor_overlay);
      *(uint32_t*)(igm_addr+MENU_NOR_EXIT_BRIDGE_OFFSET)=nor_overlay->nor_exit;
      set_supercard_mode(MAPPED_SDRAM,true,true);
    }
    if(nor_layout) {
      set_supercard_mode(MAPPED_SDRAM,true,false);
      memcpy32((void*)(GBA_ROM_BASE+nor_layout->anchor),ingame_nor_scatter_payload,ingame_nor_scatter_payload_size);
      *(uint32_t*)(igm_addr+MENU_NOR_EXIT_BRIDGE_OFFSET)=GBA_ROM_BASE+nor_layout->anchor+MENU_NOR_EXIT_OFFSET;
      set_supercard_mode(MAPPED_SDRAM,true,true);
    }
    if (use_rtc_patches) {
      set_supercard_mode(MAPPED_SDRAM, true, false);
      memcpy32(((t_igmenu*)igm_addr)->rtcfile_path, rtc_path, sizeof(rtc_path));
      set_supercard_mode(MAPPED_SDRAM, true, true);
    }
  }

  if (dsinfo)
    load_directsave_config(dsinfo);

  if (rtcinfo)
    load_rtcclock_data(rtcinfo);

  // Map the game NOR blocks. Unused blocks are zero mapped (to firmware)
  set_superchis_normap(normap);

  // Set the ROM into read only mode, disable SD card reader as well.
  set_supercard_mode(MAPPED_FIRMWARE, false, false);

  REG_WAITCNT = 0x4000;
  launch_reset(boot_bios_splash, use_fastew);

  return 0;
}

// Emulator and ROM bytes use the same bounded, halfword-safe loading path.
static NOINLINE bool load_emu_bytes(FIL *file, uint8_t *destination, unsigned size, progress_fn progress) {
  for (unsigned offset = 0; offset < size; offset += LOAD_BS) {
    unsigned requested = MIN(LOAD_BS, size-offset);
    uint32_t buffer[LOAD_BS/4]; UINT read;
    if (requested & 3) buffer[requested/4] = 0xFFFFFFFF;
    if (f_read(file, buffer, requested, &read) != FR_OK || read != requested) return false;
    set_supercard_mode(MAPPED_SDRAM, true, false);
    if (use_slowld) rom_copy_write16(destination+offset, buffer, ROUND_UP2(requested,4));
    else dma_memcpy32(destination+offset, buffer, (requested+3)/4);
    set_supercard_mode(MAPPED_SDRAM, true, true);
    if (progress && !(offset & (64*1024-1))) progress(offset,size);
  }
  return true;
}

// Generic emulator loader, including Snezziboy's precomputed SNES bank map.
NOINLINE
__attribute__((optimize("Oz"))) unsigned load_extemu_rom(const char *fn, uint32_t fs, const t_emu_loader *ldinfo, progress_fn progress) {
  FIL fd;
  uint8_t *base = (uint8_t*)GBA_ROM_ADDR, *ptr = base;
  // The external Snezziboy core uses a bank map instead of a ROM list.
  bool sfc = !strcmp(ldinfo->emu_name,"snezziboy");
  t_sfc_info info;
  if (!fs || fs > 8*1024*1024) return ERR_LOAD_BADROM;
  if (sfc) {
    if (f_open(&fd,fn,FA_READ) != FR_OK) return ERR_LOAD_BADROM;
    bool valid = f_size(&fd) == fs && sfc_inspect(&fd,fs,&info);
    f_close(&fd);
    if (!valid) return ERR_LOAD_BADROM;
  }

  if (!memcmp(ldinfo->emu_name,"vfs:",4)) {
    set_supercard_mode(MAPPED_SDRAM,true,false);
    const void *payload = get_vfile_ptr(ldinfo->emu_name+4);
    if (payload) ptr += upkr_unpack16(ptr,payload);
    set_supercard_mode(MAPPED_SDRAM,true,true);
    if (!payload) return ERR_LOAD_NOEMU;
  } else {
    char path[64]; strcpy(path,EMULATORS_PATH); strcat(path,ldinfo->emu_name); strcat(path,".gba");
    if (f_open(&fd,path,FA_READ) != FR_OK) return ERR_LOAD_NOEMU;
    unsigned size = f_size(&fd);
    bool valid = size && f_size(&fd) <= MAX_GBA_ROM_SIZE && size <= MAX_GBA_ROM_SIZE-fs &&
                 load_emu_bytes(&fd,ptr,size,NULL);
    f_close(&fd);
    if (!valid) return ERR_LOAD_NOEMU;
    ptr += ROUND_UP2(size,4);
  }
  set_supercard_mode(MAPPED_SDRAM,true,false);
  if (sfc) {
    bool valid = sfc_prepare(base,ptr-base,&info);
    set_supercard_mode(MAPPED_SDRAM,true,true);
    if (!valid) return ERR_LOAD_NOEMU;
    ptr = base+info.rom_offset;
  } else {
    if (ldinfo->hndlr) ptr += ldinfo->hndlr(ptr,fn,fs);
    set_supercard_mode(MAPPED_SDRAM,true,true);
  }
  unsigned rom_size = sfc ? info.size : fs;
  if ((unsigned)(ptr-base) > MAX_GBA_ROM_SIZE-rom_size) return ERR_LOAD_BADROM;
  if (f_open(&fd,fn,FA_READ) != FR_OK) return ERR_LOAD_BADROM;
  bool valid = f_size(&fd) == fs;
  if (valid && sfc && info.source_skip) valid = f_lseek(&fd,info.source_skip) == FR_OK;
  if (valid) valid = load_emu_bytes(&fd,ptr,rom_size,progress);
  f_close(&fd);
  if (!valid) return ERR_LOAD_BADROM;
  set_supercard_mode(MAPPED_SDRAM,true,false);
  valid = !sfc || sfc_finish(base,&info);
  set_supercard_mode(MAPPED_SDRAM,false,false);
  if (!valid) { set_supercard_mode(MAPPED_SDRAM,true,true); return ERR_LOAD_BADROM; }
  if (progress) progress(1,1);
  REG_WAITCNT = 0x4000;
  launch_reset(false,use_fastew);
  return 0;
}
