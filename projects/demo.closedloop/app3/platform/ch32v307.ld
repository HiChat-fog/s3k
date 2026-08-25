OUTPUT_ARCH(riscv) /* Specify the target architecture. */
ENTRY(_start)      /* Define the entry point of the kernel. */

__uart_base  = 0x40013800; /* Base address for USART1. */

/* CH32V307 SRAM partition layout (NAPOT power-of-two regions):
 *   PID2 attitude: 0x20000800, 26KB   PID3 comm: 0x20007000, 16KB
 *   PID1 monitor:  0x2000B000, 13KB   kernel: elsewhere (flash or tail SRAM) */
MEMORY {
    RAM (rwx) : ORIGIN = 0x20004000, LENGTH = 16K
}

SECTIONS {
    /* Code section */
    .text : {
        *(.text.init)       /* Initialization code. */
        *(.text .text.*)    /* Main code. */
    } > RAM

    /* Data section */
    .data : {
        _data = .;          /* Start of the data section. */
        *(.data .data.*)    /* Initialized data. */
        _sdata = .;         /* Start of small data section. */
        *(.sdata .sdata.*)  /* Small initialized data. */
    } > RAM

    /* BSS section */
    .bss : ALIGN (8){
        _bss = .;           /* Start of uninitialized data. */
        _sbss = .;          /* Start of the BSS section. */
        *(.sbss .sbss.*)    /* Small uninitialized data. */
        *(.bss .bss.*)      /* Uninitialized data. */
    } > RAM
    _end = ALIGN(8);    /* End of allocated sections. */

    /* Global pointer and stack */
    __global_pointer$ = MIN(_sdata + 0x800, MAX(_sdata + 0x800, _end - 0x800));
    __stack_top = ORIGIN(RAM) + LENGTH(RAM); /* Define the top of the stack. */
    __payload   = ORIGIN(RAM) + LENGTH(RAM); /* Define the payload location. */
}
