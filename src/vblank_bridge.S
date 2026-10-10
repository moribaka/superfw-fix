// Called by the game's native VBlank dispatcher in ARM System mode.
// All other interrupts keep the original dispatcher and its timing.
.syntax unified
.cpu arm7tdmi
.arm
.text
.global vblank_bridge
vblank_bridge:
  stmdb sp!, {r0-r3,r12,lr}
  mrs r0,cpsr
  stmdb sp!, {r0}
  mov r0,#0x04000000
  add r0,r0,#0x100
  ldrh r1,[r0,#0x30]
  ldr r2,=0x11111111
  ldrh r2,[r2]
  cmp r1,r2

  bne resume
  ldr r1,=0x03007ff4
  adr r2,resume
  str r2,[r1]
  msr cpsr_c,#0x92
  mov r0,#0x04000000
  ldr pc,=0x22222222
resume:
  msr cpsr_c,#0x1f
  ldmia sp!,{r0}
  msr cpsr_f,r0
  ldmia sp!,{r0-r3,r12,lr}
  ldr r0,=0x33333333
  bx r0
  .ltorg
