/*
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * 本文件按 GNU GPL 第 3 版或更新版本分发，无任何担保。
 * SRAM 读写代码取自 src/patches.S，保留其作者署名与许可证。
 * 完整许可证见 https://www.gnu.org/licenses/gpl-3.0.html。
 *
 * FSMJ v1，4 MiB，SHA256：
 * 5873d52f760825015e54e7a0f496efae39e1fed6848375904e8d77bb6c652096
 *
 * 这是数据库携带的 ROM 补丁，不是固件常驻代码。
 * 链接地址 0x080F0000；192 字节按 60/60/60/12 分为四个补丁程序，
 * 写入四份 1 MiB 镜像的 0xF0000。四份镜像的 0xF4 改为跳转到此入口，
 * 压缩数据 0x9D6E 的 0x14 改为 0，展开后的 WAITCNT 为 0x4000。
 * 所有操作放入保存补丁段，供 TF、NOR 共用；保存类型为 SRAM。
 * IRQ、RTC、游戏内固件菜单及 DirectSave 均不启用。
 */

.syntax unified
.cpu arm7tdmi
.arm
.section .text
.global init_hook
init_hook:
    @ BIOS 已解压启动器，改写其解压完成入口，保留原寄存器和栈布局。
    stmdb sp!, {r0,r1}
    ldr r0, =0x03007454
    ldr r1, =stage_hook
    str r1, [r0]
    ldmia sp!, {r0,r1}
    ldmia sp!, {r0,pc}

stage_hook:
    @ 游戏随后将显存中的函数复制到 0x02F30518、0x02F305DC 执行。
    stmdb sp!, {r0-r3,r12,lr}
    mrs r12, cpsr
    stmdb sp!, {r12}
    adr r0, read_patch
    ldr r1, =0x06007450
    mov r2, #8
1:
    ldr r3, [r0], #4
    str r3, [r1], #4
    subs r2, r2, #1
    bne 1b
    adr r0, write_patch
    ldr r1, =0x06007514
    mov r2, #8
2:
    ldr r3, [r0], #4
    str r3, [r1], #4
    subs r2, r2, #1
    bne 2b
    ldmia sp!, {r12}
    msr cpsr_f, r12
    ldmia sp!, {r0-r3,r12,lr}
    ldr pc, =0x06000080
    .pool

.thumb
.balign 4
read_patch:
    @ 与 patch_eeprom_read_sram64k 的 32 字节一致，保持块内字节顺序。
    lsls r0, r0, #22
    lsrs r0, r0, #19
    ldr r2, =0x0E000000
    adds r2, r2, r0
    adds r2, #8
    movs r0, #8
3:
    subs r2, #1
    ldrb r3, [r2]
    strb r3, [r1]
    adds r1, #1
    subs r0, #1
    bne 3b
    bx lr
    .balign 4
    .pool

write_patch:
    @ 与 patch_eeprom_write_sram64k 的 32 字节一致。
    lsls r0, r0, #22
    lsrs r0, r0, #19
    ldr r2, =0x0E000000
    adds r2, r2, r0
    adds r2, #8
    movs r0, #8
4:
    ldrb r3, [r1]
    adds r1, #1
    subs r2, #1
    strb r3, [r2]
    subs r0, #1
    bne 4b
    bx lr
    .balign 4
    .pool
