/* Monomyth v1.0 专用记录中的两个内联函数。
 * 只替换底层 ID 和单字节写入，保留游戏的设备描述表和部分扇区保存流程。
 */
.syntax unified
.cpu arm7tdmi
.thumb

.section .text.monomyth_flash_id,"ax",%progbits
.global monomyth_flash_id
.thumb_func
monomyth_flash_id:
  movs r0, #9
  lsls r0, r0, #8
  adds r0, #0xC2
  bx lr

.section .text.monomyth_write_byte,"ax",%progbits
.global monomyth_write_byte
.thumb_func
monomyth_write_byte:
  ldrb r2, [r0]
  strb r2, [r1]
  movs r0, #0
  bx lr
