#ifndef MENU_OVERLAY_H
#define MENU_OVERLAY_H
#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define MENU_OVERLAY_MAGIC 0x314F5746
#define MENU_OVERLAY_KERNEL_ROM 0x081E0000
#define MENU_OVERLAY_IWRAM_BYTES 8192
#define MENU_OVERLAY_CAPSULE_SECTORS 2048
#define MENU_OVERLAY_PAYLOAD_SECTOR 1
#define MENU_OVERLAY_FONT_SECTOR 129
#define MENU_OVERLAY_BACKUP_SECTOR 145

// Runtime values live in the reviewed bootstrap reservation, never in the
// game's SRAM or a resource that must be restored to its original fill.
#ifndef __ASSEMBLER__
typedef struct {
  uint32_t regs[8];
  uint32_t irq_sp,irq_lr,flags,waitcnt;
  uint16_t timers[4],dma[4];
  uint32_t continuation,sector,driver,backup_address;
  uint32_t resume_address,overlay_address,overlay_size,entry_address;
} t_menu_overlay_context;

typedef struct {
  uint32_t magic,revision,overlay_address,overlay_size;
  uint32_t payload_size,font_size,font_address,entry_address;
  uint32_t reserved[120];
} t_menu_overlay_config;
_Static_assert(sizeof(t_menu_overlay_config)==512,"Overlay capsule header sector");
#endif
#endif
