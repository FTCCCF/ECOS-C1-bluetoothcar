    .section .init,"ax",@progbits
    .globl _start
    .option norelax
_start:
    li sp, 0x0001fff0
    la t0, __ram_load
    la t1, __ram_start
    la t2, __ram_end
1:
    bgeu t1, t2, 2f
    lw t3, 0(t0)
    sw t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    j 1b
2:
    la t1, __bss_start
    la t2, __bss_end
3:
    bgeu t1, t2, 4f
    sw zero, 0(t1)
    addi t1, t1, 4
    j 3b
4:
    call main
5:  j 5b
