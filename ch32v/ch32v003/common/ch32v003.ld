/*
 * Linker script for CH32V003F4P6
 * 16 KB Flash, 2 KB SRAM
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 16K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 2K
}

SECTIONS
{
    /* Startup code and vector table */
    .init :
    {
        _sinit = .;
        *(.init)
        . = ALIGN(4);
    } >FLASH

    /* Program code */
    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } >FLASH

    /* Initialized data - stored in flash, copied to RAM at startup */
    .data :
    {
        _data_vma = .;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT >FLASH

    _data_lma = LOADADDR(.data);

    /* Uninitialized data - zeroed at startup */
    .bss :
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM

    /* Global pointer set to middle of small data areas */
    PROVIDE(__global_pointer$ = _data_vma + 0x400);

    /* Stack at top of RAM */
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
