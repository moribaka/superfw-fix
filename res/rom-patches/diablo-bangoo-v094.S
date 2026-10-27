// Diablo Bangoo v0.9.4：保留双 bank 日志格式，替换实际 Flash 命令。
// SHA256：dbc62344daa80a67e895b8a81f9bfb51f20d7040d8a71f8a5cb87f9746db02e3。
// 本文件编码为内置 Hack 库的逐版本操作，不链接进固件常驻代码。
// 两个 64 KiB bank 均映射到卡带 SRAM；保留游戏原有提交计数及校验。
// .blank/.identify/.erase/.program 使用已替换函数的原位置，不占用资源零区。
// 其余 bank 切换点由数据库中的 Thumb BL 操作接到本文件命名入口。
// 配套链接脚本：diablo-bangoo-v094.ld；DirectSave、RTC、固件游戏内菜单不启用。
.syntax unified
.cpu arm7tdmi
.thumb

.macro wrapper name, reg
.global \name
.thumb_func
\name:
  push {r0-r3,lr}
  movs r0,\reg
  bl bank_select
  pop {r0-r3}
  pop {pc}
.endm

.section .blank,"ax"
.global blank_check
.thumb_func
blank_check:
  push {r4-r6,lr}
  movs r4,#0
1:
  movs r0,r4
  bl bank_select
  movs r1,#7
  lsls r1,r1,#25
  movs r2,#1
  lsls r2,r2,#16
2:
  ldrb r3,[r1]
  cmp r3,#255
  bne 4f
  adds r1,#1
  subs r2,#1
  bne 2b
  adds r4,#1
  cmp r4,#2
  bne 1b
  movs r6,#1
  b 5f
4:
  movs r6,#0
5:
  movs r0,#0
  bl bank_select
  movs r0,r6
  pop {r4-r6,pc}
.balign 4
.global bank_select
.thumb_func
bank_select:
  movs r1,#1
  ands r0,r1
  lsls r0,r0,#3
  adds r0,#0x70
  ldr r1,bank_port
  ldr r2,unlock_word
  strh r2,[r1]
  strh r2,[r1]
  strh r0,[r1]
  strh r0,[r1]
  bx lr
.balign 4
bank_port: .word 0x09FFFFFE
unlock_word: .word 0x0000A55A
.global read_select_bank
.thumb_func
read_select_bank:
  push {r0-r3,lr}
  movs r0,r1
  bl bank_select
  pop {r0-r3}
  cmp r6,#0
  pop {pc}
.global read_reset_bank
.thumb_func
read_reset_bank:
  push {r0-r3,lr}
  movs r0,r2
  bl bank_select
  pop {r0-r3}
  pop {r3}
  add sp,#12
  bx r3
.balign 4

.section .identify,"ax"
.global identify_sram
.thumb_func
identify_sram:
  movs r1,#0x62
  movs r0,#0x13
  lsls r2,r0,#8
  adds r2,r2,r1
  ldr r3,id_address
  str r2,[r3]
  b 0x0806569C
.balign 4
id_address: .word 0x020015BC
.global select_first
.thumb_func
select_first:
  movs r4,#0
  b select_r4
.global select_second
.thumb_func
select_second:
  movs r4,#1
select_r4:
  push {r0-r3,lr}
  movs r0,r4
  bl bank_select
  pop {r0-r3}
  pop {pc}
.global select_r3_load_r0
.thumb_func
select_r3_load_r0:
  ldr r0,[sp,#88]
  push {r0-r3,lr}
  movs r0,r3
  bl bank_select
  pop {r0-r3}
  pop {pc}
.global select_r1_load_r2
.thumb_func
select_r1_load_r2:
  ldr r2,[sp,#88]
  push {r0-r3,lr}
  movs r0,r1
  bl bank_select
  pop {r0-r3}
  pop {pc}
.balign 4

.section .erase,"ax"
.global erase_selected
.thumb_func
erase_selected:
  ldr r1,[sp,#88]
  adds r1,r1,r0
  ldr r2,[sp,#48]
  ldr r3,sector_round
  adds r2,r2,r3
  bics r2,r3
  movs r3,#255
1:
  strb r3,[r1]
  adds r1,#1
  adds r0,#1
  cmp r0,r2
  blo 1b
  b 0x08065798
.balign 4
sector_round: .word 4095

.section .program,"ax"
.global program_byte
.thumb_func
program_byte:
  movs r2,#7
  lsls r2,r2,#25
  strb r1,[r2,r0]
  movs r0,#1
  bx lr
.global select_r0_load_r1
.thumb_func
select_r0_load_r1:
  ldr r1,[sp,#88]
  push {r0-r3,lr}
  bl bank_select
  pop {r0-r3}
  pop {pc}
.global select_r1_load_r0
.thumb_func
select_r1_load_r0:
  ldr r0,[sp,#88]
  push {r0-r3,lr}
  movs r0,r1
  bl bank_select
  pop {r0-r3}
  pop {pc}
.balign 4
