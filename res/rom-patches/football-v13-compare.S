// 欢乐足球球队球员名全汉化 v1.3：保留游戏原有的写前内容比较。
// 通过已适配的 ReadFlash 读取，供 SRAM 和 DirectSave 共用。
.syntax unified
.cpu arm7tdmi
.thumb
.thumb_set flash_read, 0x08056025

.section .football_fixed,"ax"
.global football_verify_sector
.thumb_func
football_verify_sector:
  movs r2, #128
  lsls r2, r2, #5
  b football_verify_bytes
.balign 4

.section .football_bytes,"ax"
.global football_verify_bytes
.thumb_func
football_verify_bytes:
  push {r4-r7, lr}
  sub sp, #256
  movs r4, r0
  movs r5, r1
  movs r6, r2
  movs r7, #0
  b 4f
1:
  movs r0, r4
  movs r1, r7
  mov r2, sp
  movs r3, #128
  lsls r3, r3, #1
  cmp r6, r3
  bhs 2f
  movs r3, r6
2:
  bl flash_read
  mov r0, sp
  movs r2, #128
  lsls r2, r2, #1
  cmp r6, r2
  bhs 3f
  movs r2, r6
3:
  ldrb r1, [r0]
  ldrb r3, [r5]
  cmp r1, r3
  bne 5f
  adds r0, #1
  adds r5, #1
  adds r7, #1
  subs r6, #1
  subs r2, #1
  bne 3b
4:
  cmp r6, #0
  bne 1b
  movs r0, #0
  b 6f
5:
  // 保持原接口：不相同时返回首个差异的 Flash 地址。
  lsls r0, r4, #28
  lsrs r0, r0, #16
  adds r0, r0, r7
  movs r1, #224
  lsls r1, r1, #20
  adds r0, r0, r1
6:
  add sp, #256
  pop {r4-r7}
  pop {r1}
  bx r1
.balign 4
