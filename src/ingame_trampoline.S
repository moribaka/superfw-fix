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

// This is a trampoline that can run from flash (at any address) and is
// responsible for jumping to the real in-game menu (loaded at SDRAM's 0x0)

#include "gba_regs.h"
#include "ingame.h"

// The key combo bitmask must be stored in ABORT[LR] register before booting.

.text

.global startup_entrypoint
startup_entrypoint:
  // Entry point for IGM startup
  b trampoline_startup
  .fill 7,4,0xE1A00000

.global trampoline_nocheats_handler
trampoline_nocheats_handler:
  ldrb r1, [r0, #REG_IF]       // Just read the LSB byte (r0 is 0x04000000 from BIOS)
  tst r1, $0x1                 // Check for V-Blank interrupt
  ldreq pc, [r0, #-12]         // No V-blank IRQ, resume executing the user's IRQ handler

  ldr r3, [r0, #REG_P1]        // It's actually a 16 bit reg really
  mrs r2, CPSR
  msr CPSR_c, $0x97            // Move to ABORT mode, to read LR.
  cmp lr, r3, lsl #16          // Compare the lowest 16 bits only!
  msr CPSR_cxs, r2             // Restore move, preserve "f" flags tho!
  ldrne pc, [r0, #-12]         // Key combo mismatch, resume executing the user's IRQ handler

  ldr r1, [r0, #REG_WAITCNT]   // Preserve WAITCNT, we need to go slow in SDRAM.
  push {r1}                    // Store the current WAITCNT in the IRQ handler slot (temporarily).
  str r0, [r0, #REG_WAITCNT]   // Clear WAITCNT for slow mem access

  adr ip, tramp_nocheats       // Load pool pointer
  ldmia ip!, {r0, r1, r2, r3}  // Load 4 ARM instructions
  stmdb sp , {r0, r1, r2, r3}  // Store them at SP-16
  ldmia ip , {r0, r1, r2, r3}  // Load r0-r3 constants needed by the trampoline code
  sub pc, sp, $(4*4)           // Jump to SP-16 to execute the copied insts.

  nop; nop; nop; nop
  nop; nop; nop;

.global trampoline_cheats_handler
trampoline_cheats_handler:
  ldrb r1, [r0, #REG_IF]       // Just read the LSB byte (r0 is 0x04000000 from BIOS)
  tst r1, $0x1                 // Check for V-Blank interrupt
  ldreq pc, [r0, #-12]         // No V-blank IRQ, resume executing the user's IRQ handler

  // On V-Blank we need to process cheats, so we must switch mode.

  ldr r1, [r0, #REG_WAITCNT]   // Preserve WAITCNT, we need to go slow in SDRAM.
  push {r1}                    // Push the WAITCNT to the stack temporarily
  str r0, [r0, #REG_WAITCNT]   // Clear WAITCNT for slow mem access

  adr ip, tramp_cheats         // Load pool pointer
  ldmia ip!, {r0, r1, r2, r3}  // Load 4 ARM instructions
  stmdb sp , {r0, r1, r2, r3}  // Store them at SP-16
  ldmia ip , {r0, r1, r2, r3}  // Load r0-r3 constants needed by the trampoline code
  sub pc, sp, $(4*4)           // Jump to SP-16 to execute the copied insts.

  nop; nop; nop; nop;
  nop; nop; nop; nop;
  nop; nop; nop; nop;
  nop;

.global trampoline_code_end
trampoline_code_end:

tramp_nocheats:
  strh r1, [r0]                // Map SDRAM
  strh r1, [r0]
  strh r2, [r0]
  add pc, r3, $0x08000000      // Jump to the right trampoline entry point
  .word 0x09FFFFFE
  .word 0x0000A55A
  .word 0x00000005
  .word IGM_ENTRYPOINT_NOCHEATS_OFF

tramp_cheats:
  strh r1, [r0]                // Map SDRAM
  strh r1, [r0]
  strh r2, [r0]
  add pc, r3, $0x08000000      // Jump to the right trampoline entry point
  .word 0x09FFFFFE
  .word 0x0000A55A
  .word 0x00000005
  .word IGM_ENTRYPOINT_CHEATS_OFF

// The original hotkey in ABORT[LR] is unchanged. ABORT[SP] only supplies the
// initial enabled state; old flashed trampolines ignore it and still work.
.balign 4
trampoline_startup:
  mrs r2, CPSR
  msr CPSR_c, $0x97
  mov r3, sp
  msr CPSR_cxs, r2
  cmp r3, #0
  moveq r3, #0
  movne r3, #4
  adr r1, trampoline_startup_targets
  ldr r0, [r1, r3]
  add r0, r1, r0
  mov r1, $0x04000000
  str r0, [r1, #-4]
  msr CPSR_f, r2
  mov r0, $0x08000000
  ldr pc, [r0, #0xB8]
trampoline_startup_targets:
  .word trampoline_nocheats_handler - trampoline_startup_targets
  .word trampoline_cheats_handler - trampoline_startup_targets


