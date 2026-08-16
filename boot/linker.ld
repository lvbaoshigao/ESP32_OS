/* ESP32-C6 Bootloader Linker Script — Direct Boot Mode
 * TRM 9.2: Direct Boot runs from flash via cache/MMU
 * Flash mapped at 0x4200_0000 by ROM (instruction space)
 * Data mapped at 0x4200_0000+ by ROM (data space via cache)
 * HP SRAM at 0x4080_0000 for stack and BSS
 */

ENTRY(_start)

MEMORY
{
    /* Flash mapped into instruction/data space by ROM cache/MMU */
    flash (rx)  : ORIGIN = 0x42000000, LENGTH = 4M
    /* HP SRAM for data, BSS, stack */
    sram (rwx)  : ORIGIN = 0x40800000, LENGTH = 512K
}

SECTIONS
{
    /* Boot code in flash — starts with Direct Boot magic header */
    .text.boot : ALIGN(4)
    {
        *(.text.boot)
    } > flash

    .text : ALIGN(4)
    {
        *(.text .text.*)
    } > flash

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
    } > flash

    /* Data sections in SRAM (initialized from flash by startup code if needed) */
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > sram AT > flash

    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > sram

    /* GP-relative small data anchor — 2KB below data area for ±2KB offset range */
    __global_pointer$ = __bss_end + 0x800;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
    }
}
