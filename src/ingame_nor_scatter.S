// Mirrored instructions survive the NOR/SDRAM transition without using any
// game stack bytes. Legacy trampolines remain available for existing games.
#include "menu_nor_offsets.h"
.syntax unified
.cpu arm7tdmi
.arm
.text
.global _start
_start: b startup
.word 0x32574653  // SFW2
.word 0           // exact layout id, filled while flashing
.space 20
irq_entry:
  mrs ip,cpsr
  ldrb r1,[r0,#0x202]
  tst r1,#1
  beq resume
  mrs r2,cpsr
  msr cpsr_c,#0x97
  mov r1,sp
  mov r3,lr
  msr cpsr_cxs,r2
  cmp r1,#0
  orrne ip,ip,#1
  bne enter
  ldr r1,[r0,#0x130]
  cmp r3,r1,lsl #16
  bne resume
enter:
  ldr r3,[r0,#0x204]
  str r0,[r0,#0x204]
  ldr r0,=0x09FFFFFE
  ldr r1,=0xA55A
  mov r2,#5
  msr cpsr_f,ip
  b mirror_entry
resume:
  msr cpsr_f,ip
  ldr ip,[sp,#16]
  ldr r1,[sp,#4]
  ldr r2,[sp,#8]
  ldr r3,[sp,#12]
  ldr pc,[r0,#-12]
.global mirror_entry
mirror_entry:
  strh r1,[r0]
  strh r1,[r0]
  strh r2,[r0]
  strh r2,[r0]
  tst ip,#1
  bne cheats
  msr cpsr_f,ip
  ldr pc,=MENU_NOR_ENTRY
cheats:
  msr cpsr_f,ip
  ldr pc,=MENU_NOR_CHEATS
.global mirror_exit
mirror_exit:
  strh r1,[r0]
  strh r1,[r0]
  strh r2,[r0]
  strh r2,[r0]
  mov r0,#0x04000000
  str r3,[r0,#0x204]
  ldr ip,[sp,#16]
  ldr r1,[sp,#4]
  ldr r2,[sp,#8]
  ldr r3,[sp,#12]
  ldr pc,[r0,#-12]
startup:
  adr r0,irq_entry
  mov r1,#0x04000000
  str r0,[r1,#-4]
  mov r0,#0x08000000
  ldr pc,[r0,#0xB8]
.pool

// Boot hooks retain their existing addressing and are installed after this
// template. All their handler branches land on the stable NOR bridge.
.org 0x400
.fill 8*48/4,4,0xE1A00000
.org MENU_NOR_HOOK_HANDLER
  b irq_entry
.org MENU_NOR_EXPLICIT_ENTRY
  b irq_entry
