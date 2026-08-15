# Custo de Observer_Step em instruções retiradas (CSR minstret), no ISA real
# do CH32V307 (rv32imafc/ilp32f), sob qemu-system-riscv32.
#
#   make          -> mede as variantes e imprime a tabela
#   make sizes    -> text/rodata/bss de cada variante (a LUT tem de ir p/ rodata)
#   make clean

SAME_DIR := $(abspath ../..)
SHIM     := shim
BUILD    := build

TOOLCHAIN ?= $(HOME)/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf
CC        := $(TOOLCHAIN)-gcc
SIZE      := $(TOOLCHAIN)-size
QEMU      ?= qemu-system-riscv32

# CPU do qemu fixado no ISA do QingKe V4F do CH32V307VCT6: RV32IMACF + XW
# (manual QingKeV4 V1.5, tabela 1-1). O que importa aqui é o `d=false`: se
# alguma coisa no caminho quente escorregar para double, a instrução vira
# ilegal e o bench morre, em vez de medir um soft-float que a placa não tem.
# XW é extensão de compressão da WCH (c.lbu/c.lhu/c.sb/c.sh...) — o qemu não
# a implementa e o GCC do Zephyr não a emite, então o binário medido usa só
# o subconjunto padrão. Efeito: instret aqui é um TETO; com XW o código real
# tende a ser um pouco menor, nunca maior.
QEMU_CPU  ?= rv32,m=true,a=true,f=true,d=false,c=true,zicsr=true,zifencei=true

ARCHFLAGS := -march=rv32imafc_zicsr_zifencei -mabi=ilp32f
CFLAGS    := $(ARCHFLAGS) -O2 -std=c11 -ffreestanding -Wall \
             -I$(SAME_DIR)/lib/SYS -I$(SAME_DIR)/lib/OS -I$(SHIM)
LDFLAGS   := $(ARCHFLAGS) -nostartfiles -T link.ld
LDLIBS    := -lm -lc -lgcc

SRC := bench.c start.S $(SAME_DIR)/lib/SYS/Observer_Manager.c

# v0     = baseline (libm + tanh)
# v5     = PADRÃO DO FIRMWARE (LUT + tanh) — numericamente igual ao v0
# v5_sig = LUT + sigmoide, opt-in; mais rápida, mas exige reotimizar os Γ
VARIANTS := v0 v5 v5_sig

DEFS_v0     := -DOBS_IMPL=0 -DOBS_KEEP_TANH=1
DEFS_v5     := -DOBS_IMPL=5 -DOBS_KEEP_TANH=1 -DDEF_SWITCH=SW_TANH
DEFS_v5_sig := -DOBS_IMPL=5 -DOBS_KEEP_TANH=0 -DDEF_SWITCH=SW_SIGMOID

ELVES := $(addprefix $(BUILD)/,$(addsuffix .elf,$(VARIANTS)))

.PHONY: all sizes clean

all: $(ELVES)
	@echo
	@echo "=== custo por chamada de Observer_Step — QingKe V4F ISA (RV32IMACF) ==="
	@for v in $(VARIANTS); do \
	    $(QEMU) -machine virt -cpu '$(QEMU_CPU)' -bios none -kernel $(BUILD)/$$v.elf \
	            -nographic -icount shift=0 -monitor none 2>/dev/null \
	      | tr -d '\r' | grep -a "OBS_IMPL" ; \
	done
	@echo
	@echo "rpm ~3000 e ampE > 1 => a PLL travou e o custo é o do caminho real."
	@echo
	@echo "!! mcycle_step aqui NAO e ciclo de relogio do CH32V307. O QEMU nao"
	@echo "   modela o pipeline de 3 estagios do V4F, BHT/BTB/RAS, wait state"
	@echo "   de flash nem latencia de FDIV/FSQRT — ele conta ~1 ciclo por"
	@echo "   instrucao. E o proprio CH32V307 nao implementa mcycle/minstret"
	@echo "   (manual QingKeV4 V1.5, tabela 8-1: nao ha contador de performance)."
	@echo "   Ciclo real se mede na placa com o SysTick — ver tests/README.md."

sizes: $(ELVES)
	@echo
	@echo "=== footprint por variante ==="
	@for v in $(VARIANTS); do \
	    printf '%-10s ' $$v; \
	    $(SIZE) $(BUILD)/$$v.elf | tail -1; \
	done
	@echo
	@echo "=== símbolos de libm remanescentes (V5 deve ficar vazio) ==="
	@for v in $(VARIANTS); do \
	    printf '%-10s ' $$v; \
	    $(TOOLCHAIN)-nm --size-sort -S $(BUILD)/$$v.elf \
	      | grep -aiE 'sinf|cosf|tanhf|floorf|pio2' \
	      | awk '{printf "%s(0x%s) ", $$4, $$2}' ; \
	    echo; \
	done
	@echo
	@echo "=== a LUT tem de aparecer como 'r' (.rodata); 'b'/'B' = foi p/ RAM ==="
	@$(TOOLCHAIN)-nm -S $(BUILD)/v5_sig.elf | grep -i sin_lut

$(BUILD)/%.elf: $(SRC) link.ld | $(BUILD)
	$(CC) $(CFLAGS) $(DEFS_$*) $(LDFLAGS) -o $@ bench.c start.S \
	    $(SAME_DIR)/lib/SYS/Observer_Manager.c $(LDLIBS)

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
