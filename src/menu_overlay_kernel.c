// IWRAM transport used only while a capacity-limited game is paused. Original
// ROM bytes are written once to a preallocated TF extent, then restored before
// returning. Normal loading does not scan, reread or hash the ROM.
#include <stdbool.h>
#include "menu_overlay.h"
#include "supercard_driver.h"

extern void menu_overlay_resume(t_menu_overlay_context*) __attribute__((noreturn));
extern void menu_overlay_game_reset(void) __attribute__((noreturn));

// Keep a stable transport boundary for ARM fault injection. The production
// body still uses the existing card protocol, CRC and timeout handling.
__attribute__((noipa))
unsigned menu_overlay_read(uint8_t *buffer,unsigned sector,unsigned count) {
  return sdcard_read_blocks(buffer,sector,count);
}
__attribute__((noipa))
unsigned menu_overlay_write(const uint8_t *buffer,unsigned sector,unsigned count) {
  return sdcard_write_blocks(buffer,sector,count);
}

static void resume_with(t_menu_overlay_context *context,unsigned continuation) __attribute__((noreturn));
static void resume_with(t_menu_overlay_context *context,unsigned continuation) {
  set_supercard_mode(MAPPED_SDRAM,true,false);
  context->continuation=continuation;
  menu_overlay_resume(context);
}

static void copy_words(void *destination,const void *source,unsigned bytes) {
  uint32_t *d=destination;const uint32_t *s=source;
  for(unsigned i=0;i<bytes/4;++i)d[i]=s[i];
}

static bool read_to_rom(uint32_t sector,uint32_t address,unsigned bytes,uint32_t *buffer) {
  for(unsigned offset=0;offset<bytes;offset+=512) {
    set_supercard_mode(MAPPED_SDRAM,true,true);
    if(menu_overlay_read((uint8_t*)buffer,sector+offset/512,1))return false;
    set_supercard_mode(MAPPED_SDRAM,true,false);
    unsigned n=bytes-offset;if(n>512)n=512;
    copy_words((void*)(address+offset),buffer,n);
  }
  return true;
}

static bool backup_rom(uint32_t sector,uint32_t address,unsigned bytes,uint32_t *buffer) {
  for(unsigned offset=0;offset<bytes;offset+=512) {
    set_supercard_mode(MAPPED_SDRAM,true,false);
    copy_words(buffer,(const void*)(address+offset),512);
    set_supercard_mode(MAPPED_SDRAM,true,true);
    if(menu_overlay_write((const uint8_t*)buffer,sector+offset/512,1))return false;
  }
  return true;
}

__attribute__((noreturn,externally_visible))
void menu_overlay_kernel(t_menu_overlay_context *context,unsigned phase) {
  t_menu_overlay_config config;
  uint32_t buffer[128];
  set_supercard_mode(MAPPED_SDRAM,true,false);
  unsigned sector=context->sector;
  if(phase) {
    unsigned address=context->overlay_address,size=context->overlay_size;
    bool reset=(context->entry_address&0x80000000)!=0;
    if(address<0x08000000 || address>=0x0A000000 || !size || (size&511) ||
       size>0x0A000000-address || size>(MENU_OVERLAY_CAPSULE_SECTORS-MENU_OVERLAY_BACKUP_SECTOR)*512)for(;;){}
    while(!read_to_rom(sector+MENU_OVERLAY_BACKUP_SECTOR,address,size,buffer)){}
    if(reset) {
      set_supercard_mode(MAPPED_SDRAM,true,false);
      volatile uint32_t *backup=(volatile uint32_t*)context->backup_address;
      for(unsigned i=0;i<MENU_OVERLAY_IWRAM_BYTES/4;++i)backup[i]=0;
      set_supercard_mode(MAPPED_SDRAM,false,false);
      menu_overlay_game_reset();
    }
    resume_with(context,0);
  }
  set_supercard_mode(MAPPED_SDRAM,true,true);
  if(menu_overlay_read((uint8_t*)&config,sector,1) ||
     config.magic!=MENU_OVERLAY_MAGIC || config.revision!=59 ||
     config.overlay_address<0x08000000 || config.overlay_address>=0x0A000000 ||
     !config.overlay_size || (config.overlay_size&511) ||
     config.overlay_size>(MENU_OVERLAY_CAPSULE_SECTORS-MENU_OVERLAY_BACKUP_SECTOR)*512 ||
     config.overlay_size>0x0A000000-config.overlay_address ||
     config.payload_size>64*1024 || !config.payload_size || (config.payload_size&3) ||
     config.payload_size>config.overlay_size || (config.entry_address&3) ||
     config.entry_address<config.overlay_address || config.entry_address>=config.overlay_address+config.payload_size ||
     config.font_size>8192 || !config.font_size || (config.font_size&3) ||
     config.font_address<config.overlay_address+config.payload_size ||
     config.font_address>config.overlay_address+config.overlay_size ||
     config.font_size>config.overlay_address+config.overlay_size-config.font_address) {
    // The original ROM is untouched until all required metadata is valid.
    resume_with(context,0);
  }
  if(!phase) {
    bool saved=backup_rom(sector+MENU_OVERLAY_BACKUP_SECTOR,config.overlay_address,config.overlay_size,buffer);
    if(!saved)resume_with(context,0);
    else {
      set_supercard_mode(MAPPED_SDRAM,true,false);
      context->overlay_address=config.overlay_address;
      context->overlay_size=config.overlay_size;
      context->entry_address=config.entry_address;
      bool loaded=read_to_rom(sector+MENU_OVERLAY_PAYLOAD_SECTOR,config.overlay_address,config.payload_size,buffer) &&
        read_to_rom(sector+MENU_OVERLAY_FONT_SECTOR,config.font_address,config.font_size,buffer);
      if(loaded) {
        resume_with(context,config.entry_address);
      }
      // A partial load is never resumed. Restore the saved original bytes.
      while(!read_to_rom(sector+MENU_OVERLAY_BACKUP_SECTOR,config.overlay_address,config.overlay_size,buffer)){}
      resume_with(context,0);
    }
  }
  resume_with(context,0);
}
