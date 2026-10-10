#include "menu_overlay.h"
.syntax unified
.cpu arm7tdmi
.arm
.section .overlay_start,"ax"
.global menu_overlay_start
menu_overlay_start:
  // r4 is the reviewed runtime context, r7 is the transfer phase.
  ldr sp,=0x03001F00
  ldr r2,=kernel_driver
  str r6,[r2]
  mov r0,r4
  mov r1,r7
  ldr r2,=menu_overlay_kernel
  bx r2
.pool
.text
.global sc_issdhc
.type sc_issdhc,function
sc_issdhc:
  ldr r0,kernel_driver
  mov r0,r0,lsr #31
  bx lr
.global sc_rca
.type sc_rca,function
sc_rca:
  ldr r0,kernel_driver
  mov r0,r0,lsl #16
  mov r0,r0,lsr #16
  bx lr
.global menu_overlay_resume
.type menu_overlay_resume,function
menu_overlay_resume:
  ldr pc,[r0,#80]
.global menu_overlay_game_reset
.type menu_overlay_game_reset,function
menu_overlay_game_reset:
  mov r0,#0xD3
  msr cpsr_cf,r0
  ldr sp,=0x03007FE0
  mov r0,#0x9F
  msr cpsr_cf,r0
  ldr sp,=0x03007F00
  adr r0,overlay_reset_stub
  ldr r1,=0x03007F00
  ldmia r0,{r4-r11}
  stmia r1,{r4-r11}
  bx r1
overlay_reset_stub:
  mov r0,#0xFF
  swi 0x010000
  mov r0,#0x04000000
  mov r1,#0
  strb r1,[r0,#-6]
  swi 0
  nop
  nop
.balign 4
kernel_driver: .word 0
.pool
