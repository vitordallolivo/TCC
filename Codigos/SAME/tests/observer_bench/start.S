/*
 * Boot mínimo para qemu-system-riscv32 -machine virt (-bios none: o PC entra
 * em 0x80000000). Só o necessário para chamar bench_main() com FPU e pilha.
 */

    .section .text.init
    .globl _start
_start:
    /* mstatus.FS = Dirty (0x6000). No reset FS = Off e TODA instrução de FP
     * trapeia como instrução ilegal — não dá erro numérico, dá trap. Mesmo
     * detalhe vale no CH32V307; lá quem faz isso é o CONFIG_FPU=y do Zephyr
     * (ver CLAUDE.md, "FPU: mstatus.FS não vem ligado"). */
    li      t0, 0x6000
    csrs    mstatus, t0
    fscsr   zero

    la      sp, _stack_top

    /* zera .bss */
    la      t0, _bss_start
    la      t1, _bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    call    bench_main

    /* SiFive test finisher: 0x5555 = exit(0) */
    li      t0, 0x100000
    li      t1, 0x5555
    sw      t1, 0(t0)
3:  j       3b
