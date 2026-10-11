/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
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

#include "ingame.h"
#include "irqpatch.h"
#include "menu_compact_config.h"

// In-game menu routines (ie. for IRQ patching or similar).
// Shows a small in-game menu, detected by pressing a key combo at VBlank IRQ.
// The menu stores state into SDRAM and allows users to perform certain
// operations and return to game.

// This payload is linked at IWRAM/EWRAM, where it is supposed to run.
// On entering, the asm spills some parts of IWRAM, EWRAM and VRAM into SDRAM
// and loads the menu into IWRAM/EWRAM. This way we can perform SD accesses
// as well as other tricky operations without worrying about memory mapping.

// Used structure (in SDRAM):
//  [DirSav payload], In-game payload, Spill area, Font blob, Cheat DB
//
// The spill area layout is:
//
//   I/O regs + CPU regs  (partial reg dump only)
//   Palette RAM (1KB)
//   OAM RAM (1KB)
//   VRAM (first 81KB)
//   IWRAM (first 16KB)
//   EWRAM (first 62KB)
//
// Payload size can be read from its header

#define def_symbol(fnname)             \
  .globl fnname;                       \
  fnname

#define def_function(fnname)           \
  .globl fnname;                       \
  .type fnname, function;              \
  fnname

#include "gba_regs.h"
#include "directsave.h"

// This is ARM code, placed in SDRAM (at the end of it usually). It is
// used to initialize and handle IRQs, and launch the menu from IWRAM.
// It requires some patching to complete it before copying into SDRAM.
.section    .ingame_menu_start, "ax"
.align 4
.arm

.globl ingame_menu_lang
.globl scratch_base
.globl scratch_size
.globl menu_anim_speed
.globl has_rtc_support
.globl ingame_menu_palette
.globl font_base_addr
.globl savefile_backups
.globl cheat_base_addr
.globl spill_addr
.globl savefile_pattern
.globl savestate_pattern
.globl rtcfile_path

def_symbol(ingame_menu):
  // Writes address 0x03007ffc (actually mirror 0x03fffffc) with the ingame
  // menu address (following routine). Resumes ROM startup execution.
  adr r0, ingame_menu_entrypoint_nocheats
  mov r1, $0x04000000
  str r0, [r1, #-4]

  // The start address is patched to 0xB8 (ROM header)
  mov r0, $0x08000000
  ldr pc, [r0, #0xB8]          // Reserved area, contains start address.

  nop
  nop
  nop


// Ingame menu entry via NOR trampoline
def_symbol(igm_trampoline_nocheats):
  pop {r1}                   // Save WAITCNT to the reserved slot
  str r1, saved_waitcnt

  str pc, running_on_nor     // Write non-zero value there
  b ingame_menu_entry

def_symbol(igm_trampoline_cheats):
  pop {r1}                   // Save WAITCNT to the reserved slot
  str r1, saved_waitcnt

  str pc, running_on_nor
  b ingame_menu_mid_entrypoint_cheats

// Informational data for the loader (read-only).
ingame_payload_size:
  .word (__BINARY_SIZE__ + MIN_SCRATCH_SPACE)    // How much space the payload needs

// SD card driver params.
drv_issdhc:            .word 0x0
drv_rca:               .word 0x0

// To be filled by the loader with relevant data.
ingame_menu_hotkey:    .word 0x0    // Filled with the key combo mask
ingame_menu_lang:      .word 0x0    // Filled with the language code
ingame_ds_used:        .word 0x0    // Whether DirectSave is in use.
font_base_addr:        .word 0x0    // Filled with the font data address
cheat_base_addr:       .word 0x0    // Filled with the cheat database entry address
scratch_base:          .word 0x0    // Buffer reserved for scratch space.
scratch_size:          .word 0x0
has_rtc_support:       .word 0x0    // Filled with a boolean to allow tweaking RTC values
menu_anim_speed:       .word 0x0    // Menu animation speed
ingame_menu_palette:   .fill 8,2,0  // Filled with background, foreground and select colors
savefile_backups:      .word 0x0    // Fill the number of backups to preserve.

savefile_pattern:      .fill 256, 1, 0          // File name pattern (NULL terminated)
savestate_pattern:     .fill 256, 1, 0          // File name pattern (NULL terminated)
rtcfile_path:          .fill 256, 1, 0
explicit_menu_entry:   .word ingame_menu_entry - ingame_menu

.global menu_storage_size
menu_storage_size: .word __BINARY_SIZE__
.global menu_data_offset
menu_data_offset: .word __EWRAM_SIZE__
.global menu_packed_size
menu_packed_size: .word 0


.balign IRQPATCH_HOOK_OFFSET
.global irq_boot_hooks
irq_boot_hooks:
  .fill IRQPATCH_MAX_HOOKS * IRQPATCH_HOOK_SIZE / 4, 4, 0xE1A00000


// Necessary handlers for the SC driver
def_function(sc_issdhc):
  ldr r0, drv_issdhc
  bx lr

def_function(sc_rca):
  ldr r0, drv_rca
  bx lr

// Misc fw routines:
set_cpld_mode:  // (r0: mode)
  mov r1, $0x0A000000
  ldr r2, =$0xA55A
  strh r2, [r1, #-2]
  strh r2, [r1, #-2]
  strh r0, [r1, #-2]
  strh r0, [r1, #-2]
  bx lr

// After running the menu code, some .data/.bss variables might have changed.
// This copies them back into the SD-RAM memory, so they are preserved across
// calls (menu entry and exit). We could just copy all data, but that's faster.
save_data_iwram:
  ldr r0, =__DATAS_START__
  ldr r1, =__DATAS_END__
  sub r1, r1, r0
  ldr r2, menu_data_offset
  add r2, r0, r2
  sub r2, r2, $0x03000000
  ldr r3, sob_addr
  add r2, r2, r3

  mov r3,r1
  mov r1,r2
  mov r2,r3
  b scatter_copy_to

// r0: source buffer
// r1: dest buffer
// r2: number of 32byte blocks to copy
do_spill_data:
  mov r2,r2,lsl #5
  b scatter_copy_to
restore_spill_data:
  mov r2,r2,lsl #5
  b scatter_copy_from

.balign 256
.space 8
ingame_menu_entrypoint_nocheats:

  push {r1-r2, ip}
  mrs ip, cpsr

  // Entry point, called from BIOS.
  // r0 is known to be 0x04000000 right after the BIOS IRQ handler
  ldrb r1, [r0, #REG_IF]     // Just read the LSB byte
  tst r1, $0x1               // Check for V-Blank interrupt
  beq ingame_menu_irq_resume_nocheats

  // Keep the original startup ADR and all IRQ-replay targets. The file header
  // supplies the current enabled state, so registrations cannot lose cheats.
  ldr r1, cheat_base_addr
  cmp r1, #0
  beq ingame_menu_check_hotkey
  ldr r1, [r1]
  tst r1, #0x80000000
  beq ingame_menu_check_hotkey
  msr cpsr_f, ip
  pop {r1-r2, ip}
  b ingame_menu_entrypoint_cheats

ingame_menu_check_hotkey:

  ldr r2, (ingame_menu_hotkey + 2)    // Read mask from constant pool (rotate by 16)
  ldr r1, [r0, #REG_P1]      // It's actually a 16 bit reg really
  cmp r2, r1, lsl #16        // Compare the lowest 16 bits only!
  bne ingame_menu_irq_resume_nocheats
  msr cpsr_f, ip
  pop {r1-r2, ip}
  b ingame_menu_entry

ingame_menu_irq_resume_nocheats:
  msr cpsr_f, ip
  pop {r1-r2, ip}
  ldr pc, [r0, #-12]

ingame_menu_entrypoint_cheats:

  push {r1-r3, ip}
  mrs r3, cpsr
  push {r3}

  // Same as below, but processes cheats on every v-blank as well
  ldrb r1, [r0, #REG_IF]     // Just read the LSB byte
  tst r1, $0x1               // Check for V-Blank interrupt
  beq ingame_menu_irq_resume

  // Process cheat code, this is V-Blank!
  push {r5-r6, lr}
  bl cheat_process_arm
  pop {r5-r6, lr}

  mov r0, $0x04000000
  ldr r2, (ingame_menu_hotkey + 2)    // Read mask from constant pool (rotate by 16)
  ldr r1, [r0, #REG_P1]      // It's actually a 16 bit reg really
  cmp r2, r1, lsl #16        // Compare the lowest 16 bits only!
  bne ingame_menu_irq_resume
  b ingame_menu_irq_enter

ingame_menu_irq_resume:
  pop {r3}
  msr cpsr_f, r3
  pop {r1-r3, ip}
  ldr pc, [r0, #-12]

ingame_menu_irq_enter:
  pop {r3}
  msr cpsr_f, r3
  pop {r1-r3, ip}

  // Enter menu mode!
ingame_menu_entry:
  mrs ip, cpsr

  // Enable writing to SDRAM, since we store/spill data here!
  mov r0, $0x5
  mov r1, $0x0A000000; ldr r2, =$0xA55A
  strh r2, [r1, #-2]; strh r2, [r1, #-2]
  strh r0, [r1, #-2]; strh r0, [r1, #-2]
  str ip, native_irq_cpsr
  mov r0, $0x04000000
  ldr r2, [r0, #-4]
  str r2, native_irq_vector

  // Calculate the sob_addr (payload base address)
  9: mov r0, pc;
     sub r0, $((9b - ingame_menu + 8) & ~255);
     sub r0, $((9b - ingame_menu + 8) & 255);
     str r0, sob_addr

  str sp,scatter_game_irq_sp
  ldr r1,scatter_restore_stack_offset
  add sp,r0,r1

  // Calculate the spill addr
  ldr r1, menu_storage_size
  add r1, r0
  str r1, spill_addr
  push {r0,lr}
  mov r0,r1
  bl scatter_resolve
  str r0,spill_header_addr
  mov r1,r0
  pop {r0,lr}

  // Save relevant I/O regs: timer, DMA, display... and disable them!
  // Uses ~160KiB of space. The menu must have that much space after.
  mov r3, $0

  // Backup and disable timers (8 bytes)
  ldr r0, =(0x04000000 + REG_TM0CNT)
  ldrh r2, [r0]; strh r2, [r1], #2; strh r3, [r0]
  add r0, r0, $(REG_TM1CNT - REG_TM0CNT)
  ldrh r2, [r0]; strh r2, [r1], #2; strh r3, [r0]
  add r0, r0, $(REG_TM2CNT - REG_TM1CNT)
  ldrh r2, [r0]; strh r2, [r1], #2; strh r3, [r0]
  add r0, r0, $(REG_TM3CNT - REG_TM2CNT)
  ldrh r2, [r0]; strh r2, [r1], #2; strh r3, [r0]

  // Backup and disable DMA (only control bits) (8 bytes)
  mov r0, $0x04000000
  ldrh r2, [r0, #REG_DMA0CNT_H]; strh r2, [r1], #2
  strh r3, [r0, #REG_DMA0CNT_H]
  ldrh r2, [r0, #REG_DMA1CNT_H]; strh r2, [r1], #2
  strh r3, [r0, #REG_DMA1CNT_H]
  ldrh r2, [r0, #REG_DMA2CNT_H]; strh r2, [r1], #2
  strh r3, [r0, #REG_DMA2CNT_H]
  ldrh r2, [r0, #REG_DMA3CNT_H]; strh r2, [r1], #2
  strh r3, [r0, #REG_DMA3CNT_H]

  // Backup LCD regs
  ldrh r2, [r0, #REG_DISPCNT];  strh r2, [r1], #2
  ldrh r2, [r0, #REG_DISPSTAT]; strh r2, [r1], #2
  ldrh r2, [r0, #REG_BG0CNT];   strh r2, [r1], #2
  ldrh r2, [r0, #REG_BG1CNT];   strh r2, [r1], #2
  ldrh r2, [r0, #REG_BG2CNT];   strh r2, [r1], #2
  ldrh r2, [r0, #REG_BG3CNT];   strh r2, [r1], #2
  ldrh r2, [r0, #REG_BLDCNT];   strh r2, [r1], #2
  ldrh r2, [r0, #REG_BLDALPHA]; strh r2, [r1], #2

  // Backup and stop key audio regs
  ldr r2, [r0, #REG_SOUNDCNT_L]; str r2, [r1], #4
  str r3, [r0, #REG_SOUNDCNT_L]

  // TODO save more registers that we might have missed?

  ldr ip,scatter_game_irq_sp

  // Spill all regs (r0-r3, r12 and lr are pushed on the stack by the BIOS) (64 bytes)
  ldr r0, [ip, #0]; str r0, [r1], #4    // r0
  ldr r0, [ip, #4]; str r0, [r1], #4    // r1
  ldr r0, [ip, #8]; str r0, [r1], #4    // r2
  ldr r0, [ip, #12]; str r0, [r1], #4    // r3
  stmia r1!, {r4-r11}                    // r4-r11
  ldr r0, [ip, #16]; str r0, [r1], #4    // r12
                     str ip, [r1], #4    // r13  (this is IRQ SP)
  ldr r0, [ip, #20]; str r0, [r1], #4    // r14  (this is IRQ LR, points to the IRQ'ed PC)
                     str lr, [r1], #4    // r14 return address for the BIOS handler.

  // Store also the rest of the CPU state.
  #define save_moderegs(mode)           \
    /* Change mode */                   \
    mov r0, $(0xC0 | (mode));           \
    msr cpsr_c, r0;                     \
    /* Load and write them */           \
    mov r4, sp;                         \
    mov r5, lr;                         \
    mrs r6, spsr;                       \
    stmia r1!, {r4, r5, r6};

  mrs r2, cpsr                           // CPSR IRQ mode.
  stmia r1!, {r2}

  mov r0,#0xD2
  msr cpsr_c,r0
  // A native VBlank callback can enter in System mode. In that case its
  // saved entry stack is not the banked IRQ stack.
  and r0,r2,#31
  cmp r0,#0x12
  ldreq r4,scatter_game_irq_sp
  movne r4,sp
  mov r5,lr
  mrs r6,spsr
  stmia r1!,{r4,r5,r6}
  // save_moderegs(0x12)                    // IRQ mode
  save_moderegs(0x11)                    // FIQ mode.
  save_moderegs(0x13)                    // supervisor mode.
  save_moderegs(0x17)                    // abort mode.
  save_moderegs(0x1B)                    // undefined mode.

  msr cpsr, r2                           // Restore original mode/cpsr

  ldr r1,spill_addr
  add r1,r1,#164

  // Copy Palette, OAM and VRAM to the spill area
  mov r0, $0x05000000
  mov r2, $(1024 / 8 / 4)
  bl do_spill_data

  mov r0, $0x07000000
  mov r2, $(1024 / 8 / 4)
  bl do_spill_data

  mov r0, $0x06000000
  orr r0, r0, $VRAM_SPILL_OFFSET
  mov r2, $(VRAM_SPILL_SIZE / 8 / 4)  // Swapping two frames worth of data
  bl do_spill_data

  // Swap the first 16KiB IWRAM (partial menu and stack)
  mov r0, $0x03000000
  mov r2, $(IWRAM_SPILL_SIZE / 8 / 4)
  bl do_spill_data

  // Swap the first NKiB EWRAM (menu data and code)
  mov r0, $0x02000000
  mov r2, $(EWRAM_SPILL_SIZE / 8 / 4)
  bl do_spill_data

  mov r0,#0x02000000
  ldr r1,sob_addr
  ldr r2,=__EWRAM_SIZE__
  ldr r3,menu_packed_size
  cmp r3,#0
  movne r2,#MENU_RESIDENT_BYTES
  add r2,r2,#31
  bic r2,r2,#31
  bl scatter_copy_from
  ldr r0,menu_packed_size
  cmp r0,#0
  blne menu_unpack_ewram
  mov r0,#0x03000000
  ldr r1,sob_addr
  ldr r3,menu_data_offset
  add r1,r1,r3
  ldr r2,=__IWRAM_SIZE__
  add r2,r2,#31
  bic r2,r2,#31
  bl scatter_copy_from

  // Setup stack (add some clearance) at the bottom of the first 16KB
  ldr sp, =(0x03000000 + IWRAM_SPILL_SIZE - 512)

  // If DirSav payload in use, check whether it holds the mutex
  ldr r0, ingame_ds_used
  cmp r0, $0
  blne read_sdmutex
  cmp r0, $0

  // Jump to C code, but in IWRAM copy
  adr r0, use_cheats_irq                    // Pass the ROM space address!
  ldreq r1, =ingame_menu_loop               // If dsaddr is null, or mutex is zero
  ldrne r1, =ingame_menu_blocked            // Otherwise, display warning menu
  mov lr, pc
  bx r1

  mov r0, $0x5     // Ensure we remain in R/W mode
  bl set_cpld_mode

  // Save volatile data from IWRAM back to SD-RAM.
  bl save_data_iwram
  // Restore helpers must not push onto the IWRAM they are restoring.
  ldr r0,sob_addr
  ldr r1,scatter_restore_stack_offset
  add sp,r0,r1

  // Start restoring the saved state.
  // Palette and VRAM no longer describe the menu during this copy.
  // Keep them hidden until the complete game display is restored.
  mov r0, $0x04000000
  mov r2, $0x80
  strh r2, [r0, #REG_DISPCNT]
  ldr r1, spill_addr
  add r1, $(8 + 8 + 16 + 2+2 + 64 + 64)   // Skip register save to RAM buffers.

  // Restore palette and OAM
  mov r0, $0x05000000
  mov r2, $(1024 / 8 / 4)
  bl restore_spill_data

  mov r0, $0x07000000
  mov r2, $(1024 / 8 / 4)
  bl restore_spill_data

  // Restore VRAM
  mov r0, $0x06000000
  orr r0, r0, $VRAM_SPILL_OFFSET
  mov r2, $(VRAM_SPILL_SIZE / 8 / 4)  // Swapping two frames worth of data
  bl restore_spill_data

  // Restore IWRAM and EWRAM
  mov r0, $0x03000000
  mov r2, $(IWRAM_SPILL_SIZE / 8 / 4)
  bl restore_spill_data
  mov r0, $0x02000000
  mov r2, $(EWRAM_SPILL_SIZE / 8 / 4)
  bl restore_spill_data

  // Borrowed uniform resources hold only suspended state. Restore their
  // original fill before the native game or any DMA can observe them.
  bl scatter_restore_borrowed

  // Repatch the IRQ handler just in case we activated/deactivated cheats!
  mov r1, $0x04000000

  ldr r2, use_cheats_irq
  cmp r2, $0
  adreq r0, ingame_menu_entrypoint_nocheats
  adrne r0, ingame_menu_entrypoint_cheats

  ldr r2, running_on_nor
  cmp r2, $0
  beq 1f
  ldr r2,nor_exit_bridge
  cmp r2,#0
  ldrne r0,native_irq_vector
  bne 1f

    ldr r0, [r1, #-4]          // Load the IRQ handler address
    bic r0, $0x0ff             // Find base address, payload is always 1KB aligned
    bic r0, $0x300             // Find base address, payload is always 1KB aligned
    ldr r2, use_cheats_irq
    cmp r2, $0

    addeq r0, r0, $FLASH_IGM_TRAMP_NOCHEATS_OFF
    addne r0, r0, $FLASH_IGM_TRAMP_CHEATS_OFF

1:
  // A native IWRAM dispatcher may invoke us from its VBlank callback.
  // Keep that dispatcher installed so timing-critical HBlank IRQs bypass us.
  ldr r2, native_irq_vector
  mov r3, r2, lsr $24
  cmp r3, $3
  moveq r0, r2
  str r0, [r1, #-4]

  // Make the SDRAM read-only again!
  mov r0, $0x1       // SD-RAM, Readonly mode, no SD and Bank0
  bl set_cpld_mode

  // Restore timer control regs
  ldr r1, spill_header_addr
  ldrh r11, [r1], #2
  ldrh r12, [r1], #2
  add r1, $4
  // And DMA as well
  ldrh r6, [r1], #2
  ldrh r7, [r1], #2
  ldrh r8, [r1], #2
  ldrh r9, [r1], #2

  // Restore LCD regs
  mov r2, $0x04000000
  add r1, r1, #2; // Read/restore DISPCNT at VBlank after the remaining state.
  ldrh r3, [r1], #2; strh r3, [r2, #REG_DISPSTAT];
  ldrh r3, [r1], #2; strh r3, [r2, #REG_BG0CNT];
  ldrh r3, [r1], #2; strh r3, [r2, #REG_BG1CNT];
  ldrh r3, [r1], #2; strh r3, [r2, #REG_BG2CNT];
  ldrh r3, [r1], #2; strh r3, [r2, #REG_BG3CNT];
  ldrh r3, [r1], #2; strh r3, [r2, #REG_BLDCNT];
  ldrh r3, [r1], #2; strh r3, [r2, #REG_BLDALPHA];
  // Sound regs
  ldr r3, [r1], #4; str r3, [r2, #REG_SOUNDCNT_L];

  // TODO: Need to handle flags in some way? Timers and DMA should be disabled, what about other IRQs?

  // Restore SPSR and R13/R14 registers
  #define reload_regs(mode, off)          \
    /* Change mode */                     \
    mov r0, $(0xC0 | (mode));             \
    msr cpsr_c, r0;                       \
    /* Load the regs */                   \
    ldr sp, [r1, #(64 + 4 + off*12 + 0)]; \
    ldr lr, [r1, #(64 + 4 + off*12 + 4)]; \
    ldr r3, [r1, #(64 + 4 + off*12 + 8)]; \
    msr spsr, r3;

  // New NOR bridges keep one stable native vector, reading the enabled
  // cheat flag from the same firmware-owned ABORT bank as initial startup.
  ldr r0,nor_exit_bridge
  cmp r0,#0
  beq 8f
  mrs r0,cpsr
  msr cpsr_c,#0xD7
  ldr sp,use_cheats_irq
  msr cpsr_c,r0
8:
  // These modes are never used really.
  #define reload_part_regs(mode, off)     \
    mov r0, $(0xC0 | (mode));             \
    msr cpsr_c, r0;                       \
    ldr r3, [r1, #(64 + 4 + off*12 + 8)]; \
    msr spsr, r3;

  reload_regs(0x12, 0);              // IRQ mode
  reload_regs(0x11, 1);              // FIQ mode
  reload_regs(0x13, 2);              // supervisor mode
  reload_part_regs(0x17, 3);         // abort mode
  ldr r0, has_rtc_support
  cmp r0, $0
  bne ingame_keep_clock_signature
  reload_part_regs(0x1B, 4);         // undefined mode
ingame_keep_clock_signature:

  ldr r3, [r1, #(64 + 0)]            // CPSR (IRQ mode)
  msr cpsr, r3                       // Load CPSR, changes to IRQ mode too.

  ldr sp, [r1, #(4*13)]              // Restore the stack before we actually overwrite it


  // Wait for v-blank, try to be precise to help with timing breakages
  ldr r3, =(0x04000000 + REG_TM0CNT)
1:
  ldrh r0, [r2, #REG_VCOUNT]
  cmp r0, $159
  bne 1b
1:
  ldrh r0, [r2, #REG_VCOUNT]
  cmp r0, $160
  bne 1b

  // Reveal the fully restored game at the same VBlank as its timers/DMA.
  ldr r10, spill_header_addr
  ldrh r0, [r10, #16]
  strh r0, [r2, #REG_DISPCNT]

  // Resume timers (re-enable them if they were enabled essentially)
  strh r11, [r3, #0]
  strh r12, [r3, #(REG_TM1CNT-REG_TM0CNT)]
  ldrh r0, [r10, #4]
  strh r0, [r3, #(REG_TM2CNT-REG_TM0CNT)]
  ldrh r0, [r10, #6]
  strh r0, [r3, #(REG_TM3CNT-REG_TM0CNT)]
  // Re-enable DMA as well
  strh r6, [r2, #REG_DMA0CNT_H]
  strh r7, [r2, #REG_DMA1CNT_H]
  strh r8, [r2, #REG_DMA2CNT_H]
  strh r9, [r2, #REG_DMA3CNT_H]

  // Restore regs before calling out the user IRQ
  ldr r8, [r1, #(4*12)]  // Load r0-r3 and r12/r14 into r4-r9
  ldr r9, [r1, #(4*14)]
  ldr lr, [r1, #(4*15)]  // BIOS handler ret addr

  ldmia r1!, {r4-r7}
  stmia sp, {r4-r9}      // Overwrite the BIOS stack (r0-r3, r12, r14}

  ldmia r1!, {r4-r11}    // Load save registers (r4-r11)

  // Check whether we are running from SDRAM or NOR
  ldr r1, running_on_nor
  cmp r1, $0

  // Go ahead and fallback to the user IRQ handler directly.
  bne return_to_nor
  ldr ip, native_irq_cpsr
  msr cpsr_f, ip
  ldr ip, [sp, #16]
  ldr r1, [sp, #4]
  ldr r2, [sp, #8]
  ldr r3, [sp, #12]
  mov r0, $0x04000000
  ldr pc, [r0, #-12]

  // Go back via trampoline (remaps NOR)
return_to_nor:
  mrs r0,cpsr
  ldr ip,nor_exit_bridge
  cmp ip,#0
  beq return_to_nor_legacy
  // Continue through identical ROM code present in both existing memory banks.
  // This avoids overwriting the restored game IRQ stack with trampoline code.
  ldr r0,=0x09FFFFFE
  ldr r1,=0xA55A
  ldr r2,native_irq_cpsr
  msr cpsr_f,r2
  mov r2,#0
  ldr r3,saved_waitcnt
  ldr pc,nor_exit_bridge

return_to_nor_legacy:
  msr cpsr_f,r0

  adr ip, tramp_code                 // Load pool pointer
  ldmia ip, {r0, r1, r2, r3, ip}     // Load 5 ARM instructions
  stmdb sp, {r0, r1, r2, r3, ip}     // Store them at SP-20

  adr ip, tramp_data                 // Load pool pointer
  ldmia ip, {r0, r1, r2}             // Load r0-r2 constants needed by the trampoline code
  ldr r3, saved_waitcnt              // Load the WAITCNT value to restore.
  sub pc, sp, $(4*5)                 // Jump to SP-20 to execute the copied insts.


ingame_menu_mid_entrypoint_cheats:
  // Entrypoint for cheats enabled (from NOW). Often we just process cheats.
  push {r5-r6, lr}
  bl cheat_process_arm
  pop {r5-r6, lr}

  mov r0, $0x04000000
  ldr r2, (ingame_menu_hotkey + 2)    // Read mask from constant pool (rotate by 16)
  ldr r1, [r0, #REG_P1]      // It's actually a 16 bit reg really
  cmp r2, r1, lsl #16        // Compare the lowest 16 bits only!
  bne return_to_nor          // Exiting to NOR is a bit more convoluted!
  b ingame_menu_entry        // Key combo match, launch menu


tramp_code:
  strh r1, [r0]                      // Map NOR again (read only)
  strh r1, [r0]
  strh r2, [r0]                      // Writing zero (NOR mapped, read only, etc).
  str  r3, [r2, #REG_WAITCNT]        // Restore WAITCNT
  ldr  pc, [r2, #-12]                // Jump to the user IRQ handler
tramp_data:
  .word 0x09FFFFFE
  .word 0x0000A55A
  .word 0x04000000

// Some constants that, for some reason, don't wanna go into the pool
sob_addr:
  .word 0          // Updated with the actual ROM addr where the menu lives
spill_addr:
  .word 0          // Updated with the end-of-binary address address to spill
use_cheats_irq:
  .word 0          // Whether we should be using the slower cheats IRQ hook
running_on_nor:
  .word 0          // Whether we should be returning to NOR (or SDRAM)
native_irq_cpsr:
  .word 0
native_irq_vector:
  .word 0
saved_waitcnt:
  .word 0          // Saved WAITCNT value (in case of NOR game)

// Constant pool goes here.
.balign 4
.pool

.balign 4
cheat_table:
  .word 0, 0          // Reserved for the original exported get_cheat_table ABI.

// Cheat execution machinery, processes cheat codes (pre-decoded) and executes them.

// Walk bounded entries directly; there is no fixed 63-entry pointer table.
.balign 4
cheat_process_arm:
  ldr r5, cheat_base_addr
  cmp r5, #0
  bxeq lr
  adr r0, (cheat_process_thumb + 1)
  bx r0

.thumb
cheat_process_thumb:
  push {r4, lr}
  ldr r6, [r5]
  lsl r6, #1
  lsr r6, #1
  add r5, #4

1:
  cmp r6, $0
  beq 2f
  ldrh r0, [r5]
  add r0, #8
  add r0, r5
  ldrh r4, [r5, #2]
  add r4, r0           // Next entry, preserved across both executors.
  ldrh r3, [r5, #4]
  mov r5, r4
  sub r6, #1
  cmp r3, #0
  beq 1b
  bl cheat_exec
  b 1b

2:
  pop {r4}
  pop {r0}
  bx r0

#include "cheat_exec.S"

.arm
.balign 4

// Place here all routines that are not really used within the boot section
// (they are used by the C code in cases where assembly is needed).

// Reads the SD mutex byte and returns it in r0
read_sdmutex:
  push {lr}

  mov r0, $0x1       // SD-RAM, Readonly mode, no SD (SRAM Bank0)
  bl set_cpld_mode

  ldr r4, =(0x0F000000 - DIRSAV_CFG_SIZE)
  ldrb r4, [r4, #DIRSAV_CFG_MUTEX_OFF]

  mov r0, $0x5       // SD-RAM, Read-Write mode, no SD
  bl set_cpld_mode

  mov r0, r4
  pop {pc}


// Writes the LR register for CPU undef mode (RTC register)
def_function(set_undef_lrsp):
  mrs r2, CPSR         // Save the current CPSR
  msr CPSR_c, $0x9B    // Move to undefined mode (no IRQs)
  mov lr, r0           // Store in UNDEF[LR]
  mov sp, r1           // Store in UNDEF[SP]
  ldr r3, =0xD00000DF
  msr SPSR, r3
  msr CPSR_c, r2       // Restore original model and CPSR
  bx lr

def_function(get_undef_lr):
  mrs r2, CPSR         // Save the current CPSR
  msr CPSR_c, $0x9B    // Move to undefined mode (no IRQs)
  mov r0, lr           // Read UNDEF[LR]
  msr CPSR_c, r2       // Restore original model and CPSR
  bx lr

def_function(get_undef_sp):
  mrs r2, CPSR         // Save the current CPSR
  msr CPSR_c, $0x9B    // Move to undefined mode (no IRQs)
  mov r0, sp           // Read UNDEF[SP]
  msr CPSR_c, r2       // Restore original model and CPSR
  bx lr


// Reset routines
def_function(reset_game):
  ldr r0, running_on_nor
  cmp r0, $0
  movne r0, $0x0       // NOR, readonly mode
  moveq r0, $0x1       // SD-RAM, Readonly mode
  bl set_cpld_mode
  b clear_and_reset

def_function(reset_fw):
  #ifdef SUPERCHIS_IO
    mov r0, $0x100         // Reset flash mapping
    .rept 8
      bl set_cpld_mode
    .endr
  #endif
  mov r0, $0x0               // FW mapped, read-only mode
  bl set_cpld_mode
  b clear_and_reset

// Clears a buffer (4 byte aligned) in blocks of 256 bytes
def_function(fast_mem_clr_256):
  push {r4-r11}
  mov r4, r1; mov r5, r1; mov  r6, r1; mov  r7, r1;
  mov r8, r1; mov r9, r1; mov r10, r1; mov r11, r1;
1:
  stmia r0!, {r4-r11}; stmia r0!, {r4-r11}
  stmia r0!, {r4-r11}; stmia r0!, {r4-r11}
  stmia r0!, {r4-r11}; stmia r0!, {r4-r11}
  stmia r0!, {r4-r11}; stmia r0!, {r4-r11}
  subs r2, $256
  bhi 1b

  pop {r4-r11}
  bx lr

// Copies a buffer (4 byte aligned) in blocks of 256 bytes
def_function(fast_mem_cpy_256):
  push {r4-r11}
1:
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  ldmia r1!, {r4-r11}; stmia r0!, {r4-r11}
  subs r2, $256
  bhi 1b

  pop {r4-r11}
  bx lr

def_function(get_cheat_table):
  adr r0, cheat_table
  bic r0, $0xFF000000      // Assuming calling from IW/EWRAM, clear top bits
  ldr r3, sob_addr
  add r0, r0, r3
  bx lr

def_function(clear_and_reset):
  // Setup a proper system and SVC mode stack, not sure if the game could have borked it.
  mov r0, #0xD3
  msr cpsr_cf, r0
  ldr sp, =0x03007FE0    // Default BIOS mode stack value

  mov r0, #0x9F
  msr cpsr_cf, r0        // Restore system mode.
  ldr sp, =0x03007f00    // Default BIOS value

  // Copy this subroutine into IWRAM end (last 512 bytes are not wiped)
  adr r0, 1f
  ldr r1, =0x03007f00
  mov r2, $((clear_and_reset_end - 1f) / 4)
  swi 0x0C0000           // Copy the following code into IWRAM

  ldr r1, =0x03007f00    // Continue from there.
  bx r1

1:
  // Clear memory first (all memories, save the last 256 bytes of IWRAM)
  mov r0, $0xFF
  swi 0x010000

  // Go ahead and reset to ROM (bypass BIOS)
  mov r0, $0x04000000
  mov r1, $0
  strb r1, [r0, #-6]
  swi 0
clear_and_reset_end:



// Validated scatter layout; count zero preserves the contiguous mapping.
.arm
.balign 4
.global scatter_copy_to
.type scatter_copy_to,function
scatter_copy_to:
  push {lr}
1:
  push {r0-r2}
  mov r0,r1
  bl scatter_resolve
  mov ip,r0
  mov r3,r1
  pop {r0-r2}
  cmp r3,r2
  movhi r3,r2
  sub r2,r2,r3
  add r1,r1,r3
2:
  cmp r3,#32
  blo 3f
  ldmia r0!,{r4-r11}
  stmia ip!,{r4-r11}
  sub r3,r3,#32
  b 2b
3:
  cmp r3,#0
  beq 4f
  ldr r4,[r0],#4
  str r4,[ip],#4
  sub r3,r3,#4
  b 3b
4:
  cmp r2,#0
  bne 1b
  pop {pc}

.global scatter_copy_from
.type scatter_copy_from,function
scatter_copy_from:
  push {lr}
1:
  push {r0-r2}
  mov r0,r1
  bl scatter_resolve
  mov ip,r0
  mov r3,r1
  pop {r0-r2}
  cmp r3,r2
  movhi r3,r2
  sub r2,r2,r3
  add r1,r1,r3
2:
  cmp r3,#32
  blo 3f
  ldmia ip!,{r4-r11}
  stmia r0!,{r4-r11}
  sub r3,r3,#32
  b 2b
3:
  cmp r3,#0
  beq 4f
  ldr r4,[ip],#4
  str r4,[r0],#4
  sub r3,r3,#4
  b 3b
4:
  cmp r2,#0
  bne 1b
  pop {pc}

// r0: logical ROM address; r0/r1: physical address and contiguous bytes left.
// r2-r4 are preserved. Layouts are fully validated before launching the game.
.global scatter_resolve
.type scatter_resolve,function
scatter_resolve:
  push {r2-r4,lr}
  ldr r3,sob_addr
  ldr r4,scatter_layout_offset
  add r4,r3,r4
  ldr r2,[r4],#8
  cmp r2,#0
  moveq r1,#0x40000000
  popeq {r2-r4,pc}
  sub r0,r0,r3
  mov ip,r2
1:
  ldmia r4!,{r1-r3}
  sub r1,r0,r1
  cmp r1,r2
  blo 2f
  subs ip,ip,#1
  bne 1b
  // A malformed experimental layout cannot silently access another ROM range.
3: b 3b
2:
  add r0,r3,r1
  sub r1,r2,r1
  pop {r2-r4,pc}
.pool
scatter_layout_offset: .word scatter_layout-ingame_menu
scatter_restore_stack_offset: .word scatter_restore_stack_top-ingame_menu

.balign 4
.global spill_header_addr
spill_header_addr: .word 0
.global scatter_game_irq_sp
scatter_game_irq_sp: .word 0
.global nor_exit_bridge
nor_exit_bridge: .word 0
.global scatter_nor_entry
scatter_nor_entry:
  str r3,saved_waitcnt
  mov r0,#1
  str r0,running_on_nor
  b ingame_menu_entry
.global scatter_nor_cheats
scatter_nor_cheats:
  str r3,saved_waitcnt
  str ip,native_irq_cpsr
  mov r0,#1
  str r0,running_on_nor
  msr cpsr_f,ip
  b ingame_menu_mid_entrypoint_cheats
.global scatter_layout
scatter_layout:
  .word 0,0
  .fill 64*3,4,0
.balign 8
  .fill 128,1,0
.global scatter_restore_stack_top
scatter_restore_stack_top:

.arm
.global scatter_read
.type scatter_read,function
scatter_read:
  push {r4-r11,lr}
  bl scatter_copy_from
  pop {r4-r11,lr}
  bx lr
.global scatter_write
.type scatter_write,function
scatter_write:
  push {r4-r11,lr}
  bl scatter_copy_to
  pop {r4-r11,lr}
  bx lr


.arm
.global scatter_restore_borrowed
.type scatter_restore_borrowed,function
scatter_restore_borrowed:
  ldr r0,sob_addr
  ldr r1,scatter_borrowed_offset
  add ip,r0,r1
  ldr r3,[ip],#4
1:
  cmp r3,#0
  bxeq lr
  ldmia ip!,{r0-r2}
2:
  str r2,[r0],#4
  subs r1,r1,#4
  bne 2b
  sub r3,r3,#1
  b 1b
scatter_borrowed_offset: .word scatter_borrowed-ingame_menu
.global scatter_borrowed
scatter_borrowed:
  .word 0
  .fill 8*3,4,0

.global capacity_context
capacity_context: .word 0
// Only immutable EWRAM code is packed. Execute the small input-streaming
// decoder from private IWRAM after the game state has been fully backed up.
.global menu_unpack_ewram
.type menu_unpack_ewram,function
menu_unpack_ewram:
  push {r4-r11,lr}
  mov r11,sp
  ldr sp,=(0x03000000+IWRAM_SPILL_SIZE-512)
  adr r0,menu_ap_blob_start
  ldr r1,=0x03001000
  mov r2,#((menu_ap_blob_end-menu_ap_blob_start)/32)
1:
  ldmia r0!,{r3-r10}
  stmia r1!,{r3-r10}
  subs r2,r2,#1
  bne 1b
  ldr r0,sob_addr
  ldr r2,menu_ap_resolve_offset
  add r2,r2,#0x03000000
  add r2,r2,#0x1000
  ldr r3,menu_resolver_offset
  add r3,r3,r0
  str r3,[r2]
  add r0,r0,#MENU_RESIDENT_BYTES
  ldr r1,=(0x02000000+MENU_RESIDENT_BYTES)
  ldr r2,=0x03001000
  mov lr,pc
  bx r2
  mov sp,r11
  pop {r4-r11,pc}
menu_ap_resolve_offset: .word menu_ap_resolve-menu_ap_blob_start
menu_resolver_offset: .word scatter_resolve-ingame_menu
.pool
.balign 32
.global menu_ap_blob_start
menu_ap_blob_start:
#include "menu_code_unpack.S"
.balign 32
menu_ap_blob_end:
.balign 4
.global compact_language_block
compact_language_block:
  .fill MENU_LANGUAGE_RESERVE,1,0
.pool
.global scatter_resident_end
scatter_resident_end:
