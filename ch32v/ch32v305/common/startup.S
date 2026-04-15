/*
 * Minimal startup for CH32V305 (QingKe V4F RISC-V core)
 * Vector table + reset handler
 */

    .section .init, "ax"
    .globl _start
    .align 2

_start:
    j handle_reset       /* Reset vector at flash base */

    .section .text
    .globl handle_reset
    .align 2

handle_reset:
    /* Disable interrupts */
    csrw mstatus, zero
    csrw mie, zero

    /* Set up global pointer */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* Set up stack pointer */
    la sp, _estack

    /* Copy .data section from flash to RAM */
    la a0, _data_lma
    la a1, _data_vma
    la a2, _edata
data_copy:
    beq a1, a2, data_done
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j data_copy
data_done:

    /* Zero .bss section */
    la a0, _sbss
    la a1, _ebss
bss_zero:
    beq a0, a1, bss_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j bss_zero
bss_done:

    /* Jump to main */
    call main

    /* If main returns, loop forever */
loop:
    j loop
