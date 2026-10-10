// A version-specific zero-initializer reservation must remain zero in the
// game's RAM. Replace the startup copier, never the save implementation.
.syntax unified
.cpu arm7tdmi
.thumb
.text
.global menu_overlay_initcopy
.thumb_func
menu_overlay_initcopy:
  push {r4-r6,lr}
  subs r3,r4,r2
  movs r0,#3
  adds r3,r3,r0
  bics r3,r0
  beq 4f
  ldr r5,zero_start
  ldr r6,zero_end
1:
  cmp r1,r5
  blo 2f
  cmp r1,r6
  bhs 2f
  movs r0,#0
  adds r1,#4
  b 3f
2:
  ldmia r1!,{r0}
3:
  stmia r2!,{r0}
  subs r3,#4
  bne 1b
4:
  pop {r4-r6}
  pop {r0}
  bx r0
.balign 4
.global zero_start
zero_start: .word 0
.global zero_end
zero_end: .word 0
