#include "menu_overlay.h"
.syntax unified
.cpu arm7tdmi
.arm
.text
.global overlay_bootstrap
overlay_bootstrap:
  adr r0,overlay_irq
  mov r1,#0x04000000
  str r0,[r1,#-4]
  mov r0,#0x08000000
  ldr pc,[r0,#0xB8]
.balign 32
overlay_irq:
  mrs ip,cpsr
  ldrb r1,[r0,#0x202]
  tst r1,#1
  beq irq_resume
  add r1,r0,#0x100
  ldrh r1,[r1,#0x30]
  mrs r2,cpsr
  msr cpsr_c,#0x97
  mov r3,lr
  msr cpsr_cxs,r2
  mov r3,r3,lsl #6
  mov r3,r3,lsr #22
  cmp r3,r1
  beq overlay_enter
irq_resume:
  msr cpsr_f,ip
  ldr ip,[sp,#16]
  ldr r1,[sp,#4]
  ldr r2,[sp,#8]
  ldr r3,[sp,#12]
  ldr pc,[r0,#-12]

overlay_enter:
  msr cpsr_f,ip
  ldr r0,=0x09FFFFFE
  ldr r1,=0xA55A
  mov r2,#5
  strh r1,[r0]
  strh r1,[r0]
  strh r2,[r0]
  strh r2,[r0]
  adr r0,overlay_context
  stmia r0,{r4-r11}
  str sp,[r0,#32]
  str lr,[r0,#36]
  str ip,[r0,#40]
  mov r4,r0
  mov r1,#0x04000000
  ldr r2,[r1,#0x204]
  str r2,[r4,#44]
  ldr r0,=0x04000102
  add r1,r4,#48
  mov r2,#4
  bl pause_controls
  ldr r0,=0x040000BA
  mov r2,#12
  bl pause_controls
  mov r0,#0x03000000
  ldr r1,[r4,#76]
  mov r2,#(MENU_OVERLAY_IWRAM_BYTES/32)
  bl copy_memory
  adr r4,overlay_context
  mrs r0,cpsr
  msr cpsr_c,#0xD7
  mov r5,sp
  mov r6,lr
  msr cpsr_cxs,r0
  str r5,[r4,#68]
  str r6,[r4,#72]
  mov r7,#0
  b switch_to_kernel

pause_controls:
  mov r3,#4
1:
  ldrh ip,[r0]
  strh ip,[r1],#2
  mov ip,#0
  strh ip,[r0]
  add r0,r0,r2
  subs r3,r3,#1
  bne 1b
  bx lr
copy_memory:
  ldmia r0!,{r4-r11}
  stmia r1!,{r4-r11}
  subs r2,r2,#1
  bne copy_memory
  bx lr

.global overlay_restore
overlay_restore:
  adr r4,overlay_context
  mov r1,#0x03000000
  ldr r0,[r4,#76]
  mov r2,#(MENU_OVERLAY_IWRAM_BYTES/32)
  bl copy_memory
  adr r4,overlay_context
  ldr r0,[r4,#76]
  mov r1,#0
  mov r2,#(MENU_OVERLAY_IWRAM_BYTES/4)
1:
  str r1,[r0],#4
  subs r2,r2,#1
  bne 1b
  ldr r0,=0x09FFFFFE
  ldr r1,=0xA55A
  mov r2,#1
  strh r1,[r0]
  strh r1,[r0]
  strh r2,[r0]
  strh r2,[r0]
  mov r0,#0x04000000
  ldr r1,[r4,#44]
  str r1,[r0,#0x204]
1:
  ldrh r1,[r0,#6]
  cmp r1,#159
  bne 1b
1:
  ldrh r1,[r0,#6]
  cmp r1,#160
  bne 1b
  ldr r0,=0x04000102
  add r1,r4,#48
  mov r2,#4
  bl restore_controls
  ldr r0,=0x040000BA
  mov r2,#12
  bl restore_controls
  ldr sp,[r4,#32]
  ldr lr,[r4,#36]
  ldr r3,[r4,#40]
  ldr r0,[r4,#64]
  ldmia r4,{r4-r11}
  cmp r0,#0
  beq 1f
  msr cpsr_f,r3
  bx r0
1:
  msr cpsr_f,r3
  mov r0,#0x04000000
  ldr ip,[sp,#16]
  ldr r1,[sp,#4]
  ldr r2,[sp,#8]
  ldr r3,[sp,#12]
  ldr pc,[r0,#-12]
restore_controls:
  mov r3,#4
1:
  ldrh ip,[r1],#2
  strh ip,[r0]
  add r0,r0,r2
  subs r3,r3,#1
  bne 1b
  bx lr

.global switch_to_kernel
switch_to_kernel:
  adr r8,kernel_switch_code
  ldmia r8,{r0-r3,ip,lr}
  mov r8,#0x03000000
  stmia r8,{r0-r3,ip,lr}
  ldr r0,=0x09FFFFFE
  ldr r1,=0xA55A
  mov r2,#0
  bx r8
kernel_switch_code:
  strh r1,[r0]
  strh r1,[r0]
  strh r2,[r0]
  strh r2,[r0]
  ldr pc,[pc,#-4]
  .word MENU_OVERLAY_KERNEL_ROM
.pool
.balign 16
.global overlay_context
overlay_context:
  .fill 24,4,0
.global overlay_bootstrap_end
overlay_bootstrap_end:
