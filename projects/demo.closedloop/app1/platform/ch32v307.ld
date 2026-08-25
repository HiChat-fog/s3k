OUTPUT_ARCH(riscv) /* Specify the target architecture. */
ENTRY(_start)      /* Define the entry point of the kernel. */

__uart_base  = 0x40013800; /* Base address for USART1. */

/* CH32V307 partition layout (all NAPOT power-of-two):
 *   app3 (PID3 comm):     SRAM 0x20004000 +16K  (image 14.3K)
 *   app2 (PID2 attitude): SRAM 0x20008000 +32K  (image 24.6K)
 *   app1 (PID1 monitor):  code in flash 0x08018000 (image 12.2K, image +
 *                         data LMA), writable window SRAM 0x20000000 +16K
 *                         (data+bss+stack).
 *   kernel:               flash 0x08008000, data window SRAM 0x2000F000. */
MEMORY {
    FLASH (rx) : ORIGIN = 0x08018000, LENGTH = 16K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

SECTIONS {
    /* Code section */
    .text : {
        *(.text.init)       /* Initialization code. */
        *(.text .text.*)    /* Main code. */
    } > FLASH

    .rodata : ALIGN (8) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > FLASH

    /* Data section: VMA in SRAM, LMA in flash (head.S copies it down). */
    .data ORIGIN(RAM) : AT(LOADADDR(.rodata) + SIZEOF(.rodata)) {
        _data = .;          /* Start of the data section. */
        *(.data .data.*)    /* Initialized data. */
        _sdata = .;         /* Start of small data section. */
        *(.sdata .sdata.*)  /* Small initialized data section. */
    } > RAM

    /* BSS section */
    .bss : ALIGN (8){
        _bss = .;           /* Start of uninitialized data. */
        _sbss = .;          /* Start of the BSS section. */
        *(.sbss .sbss.*)    /* Small uninitialized data. */
        *(.bss .bss.*)      /* Uninitialized data. */
    } > RAM
    _end = ALIGN(8);    /* End of allocated sections. */
    _data_lma = LOADADDR(.data);   /* Flash source for head.S copy. */

    /* Global pointer and stack */
    __global_pointer$ = MIN(_sdata + 0x800, MAX(_sdata + 0x800, _end - 0x800));
    __stack_top = ORIGIN(RAM) + LENGTH(RAM); /* Define the top of the stack. */
    __payload   = ORIGIN(RAM) + LENGTH(RAM); /* Define the payload location. */
}
