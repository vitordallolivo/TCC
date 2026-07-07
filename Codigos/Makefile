SHELL := /bin/bash
GIT_DIR := $(shell git rev-parse --show-toplevel)
CODIGOS_DIR := $(GIT_DIR)/Codigos

.PHONY: all siba same spmm native_sim native_sim_siba native_sim_same native_sim_spmm clean clean_all clean_native help

help:
	@echo "Targets disponíveis:"
	@echo ""
	@echo "  Compilação (hardware):"
	@echo "    make all         — compila SIBA, SAME e SPMM (hardware)"
	@echo "    make siba        — compila SIBA"
	@echo "    make same        — compila SAME"
	@echo "    make spmm        — compila SPMM"
	@echo ""
	@echo "  Compilação (native_sim):"
	@echo "    make native_sim  — compila native_sim de SIBA, SAME e SPMM"
	@echo "    make native_sim_siba  — compila native_sim de SIBA"
	@echo "    make native_sim_same  — compila native_sim de SAME"
	@echo "    make native_sim_spmm  — compila native_sim de SPMM"
	@echo ""
	@echo "  Limpeza:"
	@echo "    make clean       — limpa SIBA, SAME e SPMM"
	@echo "    make clean_siba  — limpa só SIBA"
	@echo "    make clean_same  — limpa só SAME"
	@echo "    make clean_spmm  — limpa só SPMM"
	@echo "    make clean_native — limpa todos os native_sim"

all: siba same spmm native_sim
	@echo "✓ Todas as boards compiladas (hardware + native_sim)"

native_sim: native_sim_siba native_sim_same native_sim_spmm
	@echo "✓ Todos os native_sim compilados"

siba:
	@echo "[SIBA] compilando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SIBA build

same:
	@echo "[SAME] compilando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SAME build

spmm:
	@echo "[SPMM] compilando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SPMM build

native_sim_siba:
	@echo "[SIBA native_sim] compilando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SIBA native_sim

native_sim_same:
	@echo "[SAME native_sim] compilando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SAME native_sim

native_sim_spmm:
	@echo "[SPMM native_sim] compilando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SPMM native_sim

clean: clean_siba clean_same clean_spmm clean_native
	@echo "✓ Todas as boards limpas"

clean_siba:
	@echo "[SIBA] limpando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SIBA clean

clean_same:
	@echo "[SAME] limpando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SAME clean

clean_spmm:
	@echo "[SPMM] limpando..."
	@$(MAKE) -C $(CODIGOS_DIR)/SPMM clean

clean_native:
	@echo "[native_sim] limpando todos..."
	@$(MAKE) -C $(CODIGOS_DIR)/SIBA native_sim_clean
	@$(MAKE) -C $(CODIGOS_DIR)/SAME native_sim_clean
	@$(MAKE) -C $(CODIGOS_DIR)/SPMM native_sim_clean
	@echo "✓ Todos os native_sim limpos"