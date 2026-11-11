// 横行霸道·复仇汉化版使用定时器 1 中断，在该中断中检查菜单热键。
// 保留游戏的中断源与原生 IRQ 处理，仅为这一 ROM 增加菜单入口。
#include "menu_nor_offsets.h"
.syntax unified
.cpu arm7tdmi
.arm
.text
.global payback_menu_irq
payback_menu_irq:
  stmdb sp!,{r1-r3,r12,lr}
  mrs r12,cpsr
  stmdb sp!,{r12}
  ldr r1,[r0,#0x130]
  mrs r2,cpsr
  msr cpsr_c,#0x97
  mov r3,lr
  msr cpsr_cxs,r2
  cmp r3,r1,lsl #16
  bne resume_native
  ldr r2,=0x08000000
  ldr r1,[r2]
  mov r1,r1,lsl #8
  add r2,r2,r1,lsr #6
  add r2,r2,#8
  ldr r1,[r2,#MENU_NOR_ENTRY_FIELD]
  add r2,r2,r1
  str r2,[sp,#20]
  ldmia sp!,{r12}
  msr cpsr_f,r12
  ldmia sp!,{r1-r3,r12,pc}
resume_native:
  ldmia sp!,{r12}
  msr cpsr_f,r12
  ldmia sp!,{r1-r3,r12,lr}
  ldr pc,=0x03000000
  .ltorg
