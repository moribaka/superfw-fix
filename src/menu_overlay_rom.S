.syntax unified
.cpu arm7tdmi
.arm
.text
.global menu_overlay_rom
menu_overlay_rom:
  ldr r0,=kernel_start
  mov r1,#0x03000000
  ldr r2,kernel_size
1:
  ldmia r0!,{r8-r11}
  stmia r1!,{r8-r11}
  subs r2,r2,#16
  bgt 1b
  ldr pc,=0x03000000
.pool
kernel_size: .word kernel_end-kernel_start
.balign 16
kernel_start:
  .incbin "menu_overlay_kernel.payload"
  .balign 16
kernel_end:
