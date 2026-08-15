/* qemu-system-riscv32 -machine virt: DRAM começa em 0x80000000. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M
}

SECTIONS
{
    .text : {
        KEEP(*(.text.init))
        *(.text .text.*)
    } > RAM

    /* .rodata é onde a LUT tem de cair (se cair em .bss, a otimização
     * perdeu o sentido — ver CLAUDE.md). O bench imprime os tamanhos. */
    .rodata : {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > RAM

    .data : {
        . = ALIGN(4);
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    . = ALIGN(16);
    . = . + 64K;
    _stack_top = .;
}
