/* ESP32-C6 Kernel Linker Script
 * Loaded by bootloader to HP SRAM at 0x40800000
 * Stack grows down from 0x4087C000
 * boot_params at 0x4087F000, mailbox at 0x4087F800 (owned by boot)
 */

ENTRY(_kernel_start)

MEMORY
{
    ram (rwx) : ORIGIN = 0x40800000, LENGTH = 480K
}

SECTIONS
{
    .text.entry : ALIGN(4)
    {
        *(.text.entry)
    } > ram

    .text : ALIGN(4)
    {
        *(.text .text.*)
    } > ram

    .init_array : ALIGN(4)
    {
        __init_array_start = .;
        *(.init_array .init_array.*)
        __init_array_end = .;
    } > ram

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
    } > ram

    .data : ALIGN(4)
    {
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
    } > ram

    .bss (NOLOAD) : ALIGN(4)
    {
        __kern_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        *(.sbss .sbss.*)
        *(SCOMMON)
        __kern_bss_end = .;
    } > ram

    __heap_start = .;
    __heap_end = 0x4087C000 - 256;      /* leave 256-byte guard below stack */
    _kern_stack_top = 0x4087C000;        /* stack grows down from here */

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
