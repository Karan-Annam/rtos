/* Linker script for QEMU mps2-an386 (ARM MPS2 board, AN386 = Cortex-M4)
 *
 * Memory map (what QEMU emulates):
 *   0x00000000  ZBT SSRAM1  (4 MB)  - we treat it as "flash": vectors + code
 *   0x20000000  ZBT SSRAM2/3 (4 MB) - RAM: .data/.bss/stacks
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

/* Top of RAM = initial MSP (handler/startup stack) */
_estack = ORIGIN(RAM) + LENGTH(RAM);

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* ARM exception-unwind tables (pulled in by libgcc); keep linker happy */
    .ARM.exidx :
    {
        *(.ARM.exidx*)
    } > FLASH

    /* Initialized data: lives in FLASH (LMA), copied to RAM (VMA) at reset */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Everything from _ebss to _estack is available as startup/handler stack */
    _end = .;
}
