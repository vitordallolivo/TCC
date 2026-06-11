SHELL := /bin/bash
BOARD ?= siba_board
PROJECT_DIR := $(HOME)/projetos/TCC/Codigos/SIBA
WEST = source $(HOME)/zephyrproject/.venv/bin/activate && cd $(HOME)/zephyrproject && west

WCH_OPENOCD := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/openocd
WCH_CFG := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/wch-riscv.cfg
GDB := $(HOME)/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gdb
ELF := $(HOME)/zephyrproject/build/zephyr/zephyr.elf

# Modo mock — roda o firmware como binário POSIX (HAL_mock.c no lugar de
# HAL.c; NCI.c continua o mesmo, com rede real via NSOS em 127.0.0.1 — ver
# CMakeLists.txt + boards/native_sim.conf), sem precisar de placa/W-Link.
# Útil para achar bugs de thread/lógica/rede na camada SYS/CMD_Core/
# Test_Shell e para testar o link UDP fim-a-fim com Aplicacao_Pi.
# Ver CLAUDE.md/HW.md "Modo Mock (native_sim)" e Aplicacao_Pi/CLAUDE.md.
NATIVE_SIM_DIR := $(PROJECT_DIR)/build_native_sim

.PHONY: build flash clean config debug native_sim native_sim_run native_sim_clean

build:
	$(WEST) build -b $(BOARD) $(PROJECT_DIR) -- -DBOARD_ROOT=$(PROJECT_DIR)
flash:
	$(WCH_OPENOCD) -f $(WCH_CFG) \
		-c "init" \
		-c "halt" \
		-c "program $(HOME)/zephyrproject/build/zephyr/zephyr.bin 0x00000000 verify reset" \
		-c "shutdown"
clean:
	$(WEST) build -b $(BOARD) $(PROJECT_DIR) --pristine -- -DBOARD_ROOT=$(PROJECT_DIR)

menuconfig:
	$(WEST) build -b $(BOARD) $(PROJECT_DIR) -t menuconfig -- -DBOARD_ROOT=$(PROJECT_DIR)

# Compila para native_sim (binário POSIX em build_native_sim/zephyr/zephyr.exe)
native_sim:
	$(WEST) build -b native_sim $(PROJECT_DIR) -d $(NATIVE_SIM_DIR) -- -DBOARD_ROOT=$(PROJECT_DIR)

# Compila (se preciso) e roda — Ctrl+C ou Ctrl+] para sair; "siba <cmd>" no
# prompt do shell para exercitar sensores/ESC/bateria/etc sem hardware
#
# -uart_stdinout: por padrão, native_sim cria uma PTY separada para a UART
# (e imprime algo como "uart connected to pseudotty: /dev/pts/N" — então o
# shell recebe seu input mas o terminal que rodou `make` não é essa PTY, e
# parece que "nada acontece"). Essa flag conecta a UART direto ao
# stdin/stdout do terminal atual, então "siba <cmd>"+Enter funciona aqui
# mesmo, sem precisar abrir outro terminal com `screen /dev/pts/N`.
native_sim_run: native_sim
	$(NATIVE_SIM_DIR)/zephyr/zephyr.exe -uart_stdinout

native_sim_clean:
	$(WEST) build -b native_sim $(PROJECT_DIR) -d $(NATIVE_SIM_DIR) --pristine -- -DBOARD_ROOT=$(PROJECT_DIR)

debug:
	@echo "Iniciando servidor de debug OpenOCD..."
	@$(WCH_OPENOCD) -f $(WCH_CFG) > /dev/null 2>&1 & \
	PID=$$!; \
	echo "Aguardando OpenOCD (PID: $$PID)..."; \
	sleep 2; \
	$(GDB) $(ELF) \
		-ex "target extended-remote :3333" \
		-ex "monitor halt" \
		-ex "load" \
		-ex "monitor reset halt"; \
	kill $$PID