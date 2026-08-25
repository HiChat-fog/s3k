OUTPUT_ARCH(riscv)
ENTRY(_start)

__uart_base  = 0x03002000;

MEMORY {
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 64K
}

SECTIONS {
    .text : {
        *(.text.init)
        *(.text .text.*)
    } > RAM

    .data : {
        _data = .;
        *(.data .data.*)
        _sdata = .;
        *(.sdata .sdata.*)
    } > RAM

    .bss : ALIGN (8) {
        _bss = .;
        _sbss = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
    } > RAM
    _end = ALIGN(8);

    __global_pointer$ = MIN(_sdata + 0x800, MAX(_sdata + 0x800, _end - 0x800));
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __payload   = ORIGIN(RAM) + LENGTH(RAM);
}
