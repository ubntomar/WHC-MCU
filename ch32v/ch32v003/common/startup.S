/*
 * Minimal startup for CH32V003 (QingKe V2A RISC-V core)
 * Vector table + reset handler
 */

    .section .init, "ax"
    .globl _start
    .align 2

_start:
    j handle_reset       /* Reset vector at 0x00000000 */

    .section .text
    .globl handle_reset
    .align 2

handle_reset:
    /* Disable interrupts */
    csrw mstatus, zero

    /* Set up global pointer */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* Set up stack pointer */
    la sp, _estack

    /* Copy .data section from flash to RAM */
    la a0, _data_lma       /* Source (in flash) */
    la a1, _data_vma       /* Destination (in RAM) */
    la a2, _edata          /* End of .data in RAM */
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
