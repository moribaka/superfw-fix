.syntax unified
.cpu arm7tdmi
.section .text
.balign 4
.global rtc_custom_payload_start
rtc_custom_payload_start:
.word 512
.word rtc_custom_reader_zero - rtc_custom_payload_start
.word rtc_custom_time - rtc_custom_payload_start
.word 688
.word rtc_custom_set_time - rtc_custom_payload_start
.word 844
.word rtc_custom_status - rtc_custom_payload_start
.word rtc_custom_success - rtc_custom_payload_start
.word rtc_custom_initialize - rtc_custom_payload_start
.word rtc_custom_ready_probe - rtc_custom_payload_start
.word 4
.word rtc_custom_relocations - rtc_custom_payload_start
.word rtc_custom_ready - rtc_custom_payload_start
.word rtc_custom_status_address - rtc_custom_payload_start
.word rtc_custom_payload_end - rtc_custom_payload_start
.word rtc_custom_global_decimal - rtc_custom_payload_start

.thumb
.balign 4
rtc_custom_reader_zero:
push {r4, lr}
ldr r3, rtc_custom_reader_pointer
bl rtc_custom_indirect
movs r0, #0
pop {r4, pc}
.balign 4
rtc_custom_time:
push {r4, lr}
sub sp, #8
movs r4, r0
mov r0, sp
ldr r3, rtc_custom_reader_pointer
bl rtc_custom_indirect
mov r1, sp
ldrb r0, [r1, #4]
strb r0, [r4, #4]
ldrb r0, [r1, #5]
strb r0, [r4, #5]
ldrb r0, [r1, #6]
strb r0, [r4, #6]
movs r0, #1
add sp, #8
pop {r4, pc}
.balign 4
rtc_custom_set_time:
push {r4, lr}
sub sp, #8
movs r4, r0
mov r0, sp
ldr r3, rtc_custom_reader_pointer
bl rtc_custom_indirect
mov r1, sp
ldrb r0, [r4, #4]
strb r0, [r1, #4]
ldrb r0, [r4, #5]
strb r0, [r1, #5]
ldrb r0, [r4, #6]
strb r0, [r1, #6]
mov r0, sp
ldr r3, rtc_custom_setter_pointer
bl rtc_custom_indirect
add sp, #8
pop {r4, pc}
.balign 4
rtc_custom_status:
movs r1, #0x40
strb r1, [r0, #7]
movs r0, #1
bx lr
.balign 4
rtc_custom_success:
movs r0, #1
bx lr
.balign 4
rtc_custom_initialize:
push {r4, lr}
ldr r4, rtc_custom_ready
movs r0, #1
strb r0, [r4]
ldr r1, rtc_custom_status_address
movs r0, #0x40
strb r0, [r1]
movs r0, r4
adds r0, #1
ldr r3, rtc_custom_reader_pointer
bl rtc_custom_indirect
movs r0, #0
pop {r4, pc}
.balign 4
rtc_custom_ready_probe:
ldr r1, rtc_custom_ready
movs r0, #1
strb r0, [r1]
ldr r1, rtc_custom_status_address
movs r0, #0x40
strb r0, [r1]
movs r0, #1
bx lr
.balign 4
rtc_custom_global_decimal:
push {r4, r5, lr}
movs r4, r0
ldr r3, rtc_custom_reader_pointer
bl rtc_custom_indirect
movs r1, #7
movs r5, #6
1:
ldrb r3, [r4]
lsrs r0, r3, #4
muls r0, r5
subs r3, r3, r0
strb r3, [r4]
adds r4, #1
subs r1, #1
bne 1b
pop {r4, r5, pc}
rtc_custom_indirect:
bx r3
.balign 4
rtc_custom_reader_pointer:
.word 513
rtc_custom_setter_pointer:
.word 689
rtc_custom_ready:
.word 0
rtc_custom_status_address:
.word 0
rtc_custom_months:
.byte 31,28,31,30,31,30,31,31,30,31,30,31
.byte 31,29,31,30,31,30,31,31,30,31,30,31
.balign 4
rtc_custom_relocations:
.word 684, rtc_custom_months - rtc_custom_payload_start
.word 840, rtc_custom_months - rtc_custom_payload_start
.word rtc_custom_reader_pointer - rtc_custom_payload_start, 513
.word rtc_custom_setter_pointer - rtc_custom_payload_start, 689
rtc_custom_payload_end:
