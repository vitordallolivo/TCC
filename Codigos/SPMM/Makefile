SHELL := /bin/bash
BOARD ?= spmm_board
PROJECT_DIR := $(HOME)/Documentos/Facul/TCC/Codigos/SPMM
WEST = source $(HOME)/zephyrproject/.venv/bin/activate && cd $(HOME)/zephyrproject && west

WCH_OPENOCD := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/openocd
WCH_CFG := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/wch-riscv.cfg
GDB := $(HOME)/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gdb
ELF := $(HOME)/zephyrproject/build/zephyr/zephyr.elf

# native_sim (POSIX): roda o firmware como binario no host, sem placa/W-Link.
# Util para depurar shell/parsing/empacotamento UDP fora do hardware.
# Ver CLAUDE.md "Copiar o setup de POSIX da SIBA".
NATIVE_SIM_DIR := $(PROJECT_DIR)/build_native_sim

.PHONY: build flash clean menuconfig debug native_sim native_sim_run native_sim_clean

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

# Compila para native_sim (binario POSIX em build_native_sim/zephyr/zephyr.exe)
native_sim:
	$(WEST) build -b native_sim $(PROJECT_DIR) -d $(NATIVE_SIM_DIR) -- -DBOARD_ROOT=$(PROJECT_DIR)

# Compila (se preciso) e roda. -uart_stdinout liga a UART ao stdin/stdout do
# terminal atual, entao "throttle 1500"+Enter funciona aqui mesmo (o shell do
# Zephyr aparece como "uart:~$"). Ctrl+C para sair.
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
