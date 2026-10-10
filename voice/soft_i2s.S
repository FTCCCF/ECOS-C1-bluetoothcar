#ifndef VOICE_MIC_SD_GPIO
#define VOICE_MIC_SD_GPIO 5
#endif
    .section .text.soft_i2s_alt_mono,"ax",@progbits
    .globl soft_i2s_alt_mono
    .option norvc
    .option norelax
/* The v5 SRAM clock loop and shift8 decode, with a persistent output word.
 * Audio never clears the motor or LCD GPIO bits. SD is configurable, and
 * the C header rejects overlap with motor, LCD, SCK, or WS outputs. */
    .macro BIT low, high, accum
    sw \low, 0(s0)
    .rept 7
    nop
    .endr
    sw \high, 0(s0)
    lw t0, 0(s0)
#if VOICE_MIC_SD_GPIO < 11
    andi t0, t0, (1 << VOICE_MIC_SD_GPIO)
#else
    li t1, (1 << VOICE_MIC_SD_GPIO)
    and t0, t0, t1
#endif
    srli t0, t0, VOICE_MIC_SD_GPIO
    slli \accum, \accum, 1
    or \accum, \accum, t0
    .endm
soft_i2s_alt_mono:
    addi sp, sp, -48
    sw s0, 0(sp)
    sw s1, 4(sp)
    sw s2, 8(sp)
    sw s3, 12(sp)
    sw s4, 16(sp)
    sw s5, 20(sp)
    sw s6, 24(sp)
    sw s7, 28(sp)
    sw s8, 32(sp)
    li s0, 0x03000000
    li t0, -769
    and s1, a3, t0
    ori s2, s1, 512
    ori s3, s1, 256
    ori s4, s1, 768
    mv s5, a1
    mv s6, a2
    mv s7, a0
    rdcycle s8
frame:
    li a4, 0
    li a5, 0
    .rept 32
    BIT s1, s2, a4
    .endr
    .rept 32
    BIT s3, s4, a5
    .endr
    bnez s5, warmup
    srai a4, a4, 16
    sh a4, 0(s7)
    addi s7, s7, 2
    addi s6, s6, -1
    bnez s6, frame
    rdcycle a0
    sub a0, a0, s8
    sw s1, 0(s0)
    lw s0, 0(sp)
    lw s1, 4(sp)
    lw s2, 8(sp)
    lw s3, 12(sp)
    lw s4, 16(sp)
    lw s5, 20(sp)
    lw s6, 24(sp)
    lw s7, 28(sp)
    lw s8, 32(sp)
    addi sp, sp, 48
    ret
warmup:
    addi s5, s5, -1
    bnez s5, frame
    rdcycle s8
    j frame

/* Continuous clocks while awaiting a quiet-to-voice transition. The first
 * 2048 buffer samples are a rolling prefix, followed by the post-trigger
 * samples. C rotates that prefix after capture; no GPIO clock is paused for
 * copying. 16 block timestamps measure the complete captured interval. */
    .section .text.soft_i2s_triggered_mono,"ax",@progbits
    .globl soft_i2s_triggered_mono
soft_i2s_triggered_mono:
    addi sp, sp, -64
    sw s0, 0(sp)
    sw s1, 4(sp)
    sw s2, 8(sp)
    sw s3, 12(sp)
    sw s4, 16(sp)
    sw s5, 20(sp)
    sw s6, 24(sp)
    sw s7, 28(sp)
    sw s8, 32(sp)
    sw s9, 36(sp)
    sw s10, 40(sp)
    sw s11, 44(sp)
    sw a1, 48(sp)
    li s0, 0x03000000
    li t0, -769
    and s1, a3, t0
    ori s2, s1, 512
    ori s3, s1, 256
    ori s4, s1, 768
    mv s5, a1
    addi s6, a2, -2048
    mv s7, a0
    mv s9, a4
    li s10, 0
    li s11, 0
    li t2, 0                 /* DC estimate, Q8 */
    li t3, 0                 /* 128-frame absolute-energy sum */
    li t4, 0                 /* block frame count */
    li t5, 64                /* quiet absolute-energy estimate */
    li t6, 0                 /* quiet blocks, saturating at two */
    li a6, 0                 /* consecutive high-energy blocks */
    li a7, 0                 /* total frames before trigger */
    sw zero, 16(s9)
    blez s6, .Lvoice_abort
.Lvoice_frame:
    bnez s11, .Lvoice_bits
    andi t0, s10, 127
    bnez t0, .Lvoice_bits
    srli t0, s10, 7
    slli t0, t0, 2
    addi t0, t0, 20
    add t0, s9, t0
    rdcycle t1
    sw t1, 0(t0)
.Lvoice_bits:
    li a4, 0
    li a5, 0
    .rept 32
    BIT s1, s2, a4
    .endr
    .rept 32
    BIT s3, s4, a5
    .endr
    srai a4, a4, 16
    bnez s11, .Lvoice_store
    slli t0, s10, 1
    add t0, a0, t0
    sh a4, 0(t0)
    addi s10, s10, 1
    andi s10, s10, 2047
    addi a7, a7, 1
    slli t0, a4, 8
    sub t0, t0, t2
    srai t0, t0, 8
    add t2, t2, t0
    srai t0, t2, 8
    sub t0, a4, t0
    bgez t0, .Lvoice_abs
    neg t0, t0
.Lvoice_abs:
    add t3, t3, t0
    addi t4, t4, 1
    beqz s5, .Lvoice_block
    addi s5, s5, -1
    bnez s5, .Lvoice_block
    li t3, 0
    li t4, 0
    li t6, 0
    li a6, 0
    j .Lvoice_poll
.Lvoice_block:
    li t0, 128
    bltu t4, t0, .Lvoice_frame
    srli a1, t3, 7
    li t3, 0
    li t4, 0
    slli a2, t5, 1
    li t0, 128
    bgeu a2, t0, .Lvoice_threshold
    mv a2, t0
.Lvoice_threshold:
    bnez s5, .Lvoice_update_noise
    bgeu a1, a2, .Lvoice_noise_done
.Lvoice_update_noise:
    sub t0, a1, t5
    srai t0, t0, 4
    add t5, t5, t0
    li t0, 16
    bge t5, t0, .Lvoice_noise_done
    mv t5, t0
.Lvoice_noise_done:
    bnez s5, .Lvoice_poll
    li t0, 2048
    bltu a7, t0, .Lvoice_poll
    srli t0, t5, 1
    add t0, t0, t5
    li t1, 96
    bgeu t0, t1, .Lvoice_quiet_threshold
    mv t0, t1
.Lvoice_quiet_threshold:
    bgtu a1, t0, .Lvoice_high
    li a6, 0
    li t0, 2
    bgeu t6, t0, .Lvoice_poll
    addi t6, t6, 1
    j .Lvoice_poll
.Lvoice_high:
    bltu a1, a2, .Lvoice_reset_high
    li t0, 2
    bltu t6, t0, .Lvoice_reset_high
    addi a6, a6, 1
    li t0, 6                 /* ~45ms: exclude brief clicks before speech */
    bltu a6, t0, .Lvoice_poll
    sw s10, 0(s9)
    sw a1, 4(s9)
    sw t5, 8(s9)
    lw t0, 48(sp)
    sub t0, a7, t0
    sw t0, 12(s9)
    srli t0, s10, 7
    slli t0, t0, 2
    addi t0, t0, 20
    add t0, s9, t0
    lw s8, 0(t0)
    li t0, 4096
    add s7, a0, t0
    li s11, 1
    j .Lvoice_poll
.Lvoice_reset_high:
    li a6, 0
.Lvoice_poll:
    lw t0, 20(s0)
    li t1, 115               /* UART 's': abort and let C stop motors */
    beq t0, t1, .Lvoice_abort
    j .Lvoice_frame
.Lvoice_store:
    sh a4, 0(s7)
    addi s7, s7, 2
    addi s6, s6, -1
    andi t0, s6, 127
    bnez t0, .Lvoice_frame
    lw t0, 20(s0)
    li t1, 115
    beq t0, t1, .Lvoice_abort
    bnez s6, .Lvoice_frame
    rdcycle a0
    sub a0, a0, s8
    j .Lvoice_return
.Lvoice_abort:
    li t0, 1
    sw t0, 16(s9)
    li a0, 0
.Lvoice_return:
    sw s1, 0(s0)
    lw s0, 0(sp)
    lw s1, 4(sp)
    lw s2, 8(sp)
    lw s3, 12(sp)
    lw s4, 16(sp)
    lw s5, 20(sp)
    lw s6, 24(sp)
    lw s7, 28(sp)
    lw s8, 32(sp)
    lw s9, 36(sp)
    lw s10, 40(sp)
    lw s11, 44(sp)
    addi sp, sp, 64
    ret
