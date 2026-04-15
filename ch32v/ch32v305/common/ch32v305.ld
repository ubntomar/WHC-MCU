/*
 * Linker script for CH32V305FBP6
 * 128 KB Flash, 32 KB SRAM
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
}

SECTIONS
{
    .init :
    {
        _sinit = .;
        *(.init)
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } >FLASH

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

    PROVIDE(__global_pointer$ = _data_vma + 0x800);

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
