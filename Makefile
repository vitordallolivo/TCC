SHELL := /bin/bash
ROOT := $(shell git rev-parse --show-toplevel)

.PHONY: help firmware flash native_sim native_sim_run clean protocolo gui documento apresentacao

help:
	@echo "Atalhos do repositório:"
	@echo ""
	@echo "  Firmware (placa de medição):"
	@echo "    make firmware        — compila para a placa real"
	@echo "    make flash           — grava na placa"
	@echo "    make native_sim      — compila para rodar no PC (Linux)"
	@echo "    make native_sim_run  — roda o native_sim"
	@echo "    make clean           — limpa os builds do firmware"
	@echo ""
	@echo "  Protocolo e aplicação:"
	@echo "    make protocolo       — gera os headers C a partir de gui/Config/SIBA.yaml"
	@echo "    make gui             — abre a aplicação de supervisão"
	@echo ""
	@echo "  Documentos:"
	@echo "    make documento       — compila o texto do TCC"
	@echo "    make apresentacao    — compila a apresentação"

firmware:
	@$(MAKE) -C $(ROOT)/firmware build

flash:
	@$(MAKE) -C $(ROOT)/firmware flash

native_sim:
	@$(MAKE) -C $(ROOT)/firmware native_sim

native_sim_run:
	@$(MAKE) -C $(ROOT)/firmware native_sim_run

clean:
	@$(MAKE) -C $(ROOT)/firmware clean
	@$(MAKE) -C $(ROOT)/firmware native_sim_clean

protocolo:
	@cd $(ROOT)/gui && .venv/bin/python Config/generate.py

gui:
	@cd $(ROOT)/gui && .venv/bin/python mainApp.py

documento:
	@$(MAKE) -C $(ROOT)/docs/TCC_documento

apresentacao:
	@$(MAKE) -C $(ROOT)/docs/TCC_apresentacao
