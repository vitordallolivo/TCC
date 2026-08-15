# Equivalência numérica V0 x V5 do Observer_Step (plano de validação do
# CLAUDE.md). Compila o Observer_Manager.c REAL do firmware contra um shim
# mínimo de Zephyr, uma vez por variante, e compara as saídas.
#
#   make            -> A/B com os Γ do autotuning (o regime que importa)
#   make factory    -> mesmo A/B com os DEF_GAMMA* de fábrica
#   make isolate    -> separa o erro da LUT do erro do sigmoide
#   make lut        -> regenera lib/SYS/Observer_Sin_LUT.h
#   make clean

SAME_DIR  := $(abspath ../..)
BUILD     := build

CC        ?= cc
CFLAGS    := -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter \
             -I$(SAME_DIR)/lib/SYS -I$(SAME_DIR)/lib/OS -Ishim
LDLIBS    := -lm

SRC       := harness.c $(SAME_DIR)/lib/SYS/Observer_Manager.c
EXES      := $(BUILD)/obs_v0 $(BUILD)/obs_v5 $(BUILD)/obs_v5_sig

.PHONY: all factory isolate lut clean

all: $(EXES)
	@./$(BUILD)/obs_v0 $(BUILD)/v0_tuned.csv --tuned
	@./$(BUILD)/obs_v5 $(BUILD)/v5_tuned.csv --tuned
	@python3 compare.py --ref $(BUILD)/v0_tuned.csv --new $(BUILD)/v5_tuned.csv \
	    --label "Γ do autotuning — V0 (libm+tanh) x V5 (LUT+tanh, padrao do firmware)"

factory: $(EXES)
	@./$(BUILD)/obs_v0 $(BUILD)/v0_fab.csv
	@./$(BUILD)/obs_v5 $(BUILD)/v5_fab.csv
	@python3 compare.py --ref $(BUILD)/v0_fab.csv --new $(BUILD)/v5_fab.csv \
	    --label "Γ de fábrica — V0 (libm+tanh) x V5 (LUT+tanh, padrao do firmware)"

# Separa as duas fontes de desvio: com a MESMA função de comutação nos dois
# lados sobra só o erro da LUT+identidade; V5 tanh x V5 sigmoide isola a
# troca da comutação. É este experimento que mostra que 100 % do desvio vem
# da comutação e nenhum da LUT.
isolate: $(EXES)
	@./$(BUILD)/obs_v0     $(BUILD)/v0_tuned.csv     --tuned
	@./$(BUILD)/obs_v5     $(BUILD)/v5_tuned.csv     --tuned
	@./$(BUILD)/obs_v5_sig $(BUILD)/v5_sig_tuned.csv --tuned
	@echo "--- só LUT + identidade (tanh nos dois lados) ---"
	@python3 compare.py --ref $(BUILD)/v0_tuned.csv --new $(BUILD)/v5_tuned.csv \
	    --label "V0 (libm+tanh) x V5 (LUT+tanh)" || true
	@echo
	@echo "--- só a troca tanh -> sigmoide (LUT nos dois lados) ---"
	@python3 compare.py --ref $(BUILD)/v5_tuned.csv --new $(BUILD)/v5_sig_tuned.csv \
	    --label "V5 (LUT+tanh) x V5 (LUT+sigmoide)" || true

$(BUILD)/obs_v0: $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) -DOBS_IMPL=0 -o $@ $(SRC) $(LDLIBS)

$(BUILD)/obs_v5: $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) -DOBS_IMPL=5 -o $@ $(SRC) $(LDLIBS)

# V5 com a comutação sigmoide (tanh compilado fora) — a variante mais rápida,
# mas que exige reotimizar os Γ. Aqui serve para MEDIR o quanto essa troca
# custa em exatidão; não é o padrão do firmware.
$(BUILD)/obs_v5_sig: $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) -DOBS_IMPL=5 -DOBS_KEEP_TANH=0 -DDEF_SWITCH=SW_SIGMOID \
	    -o $@ $(SRC) $(LDLIBS)

$(BUILD):
	@mkdir -p $(BUILD)

lut:
	@python3 gen_sin_lut.py $(SAME_DIR)/lib/SYS/Observer_Sin_LUT.h

clean:
	@rm -rf $(BUILD)
