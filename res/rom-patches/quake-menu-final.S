// 仅适用 SHA256 1cc084d228983744ac954dc0a40f847f442f124f8369438b17625270b00a7404。
// 公共部分在 TF 与 NOR 共用；NOR 条件写入见 operation-source.json。
// 私有包装使用当前 SVC 栈，不覆盖原 IRQ/BIOS 帧，并保留游戏使用的 ABT/UND SP/LR。
// NOR 热键与金手指开关只在真正 CRT 尾部捕获，菜单前暂还原入口参数，返回后同步开关再恢复渲染器。
// NOR 进入现有 explicit+4 分支；IP 的 bit0 是真实金手指标志，不把 SVC 模式位误作标志。
.syntax unified
.cpu arm7tdmi
.arm
.text
.global entry
entry:
 msr cpsr_c,#0x93
 stmdb sp!,{r0-r3,r12,lr}
 mrs r12,cpsr
 stmdb sp!,{r12}
 mov r0,#0x04000000
 add r1,r0,#0x100
 ldrh r1,[r1,#0x30]
key_start:
 ldr r2,hotkey
 ldrh r2,[r2]
 cmp r1,r2
 bne restore
 ldr r1,=0x03007ff4
 ldr r2,[r1,#8]
 stmdb sp!,{r2}
 ldr r2,[r1]
 stmdb sp!,{r2}
 mrs r2,cpsr
 msr cpsr_c,#0x97
 mov r3,sp
 mov r12,lr
 msr cpsr_c,r2
 stmdb sp!,{r3,r12}
 mrs r2,cpsr
 msr cpsr_c,#0x9b
 mov r3,sp
 mov r12,lr
 msr cpsr_c,r2
 stmdb sp!,{r3,r12}
nor_config:
 nop
nor_config_end:
 adr r2,resume
 str r2,[r1]
 ldr pc,target
resume:
nor_post_config:
 nop
nor_post_config_end:
 ldmia sp!,{r3,r12}
 mrs r2,cpsr
 msr cpsr_c,#0x9b
 mov sp,r3
 mov lr,r12
 msr cpsr_c,r2
 ldmia sp!,{r3,r12}
 mrs r2,cpsr
 msr cpsr_c,#0x97
 mov sp,r3
 mov lr,r12
 msr cpsr_c,r2
 ldmia sp!,{r2}
 ldr r1,=0x03007ff4
 str r2,[r1]
 ldmia sp!,{r2}
 str r2,[r1,#8]
restore:
 ldmia sp!,{r12}
 msr cpsr_f,r12
 ldmia sp!,{r0-r3,r12,lr}
 msr cpsr_c,#0x92
 ldr pc,=0x030071c4
cfg_enter:
 mrs r2,cpsr
 msr cpsr_c,#0x97
 ldr r3,nor_cfg_ptr
 ldr lr,[r3]
 and sp,lr,#1
 bic ip,r2,#1
 orr ip,ip,sp
 bic lr,lr,#1
 msr cpsr_c,r2
 bx lr
cfg_exit:
 mrs r2,cpsr
 msr cpsr_c,#0x97
 mov r3,lr
 orr r3,r3,sp
 msr cpsr_c,r2
 ldr r2,nor_cfg_ptr
 str r3,[r2]
 bx lr
target: .word 0x08144ac0
hotkey: .word 0x0814444c
nor_cfg_ptr: .word 0x03007ff0
 .ltorg

// 此段只由 NOR 的原 CRT 尾部跳转进入，重放原尾跳到 08046990。
.org 320,0xff
.global quake_cache_init
quake_cache_init:
 stmdb sp!,{r0-r2}
 mrs r2,cpsr
 msr cpsr_c,#0x97
 mov r0,lr
 orr r0,r0,sp
 msr cpsr_c,r2
 ldr r1,cache_address
 str r0,[r1]
 ldmia sp!,{r0-r2}
 ldr pc,cache_resume
cache_address: .word 0x03007ff0
cache_resume: .word 0x08046990
