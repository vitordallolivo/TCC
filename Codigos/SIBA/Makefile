SHELL := /bin/bash
BOARD ?= siba_board
PROJECT_DIR := $(HOME)/Documentos/Facul/TCC/Codigos/SIBA
WEST = source $(HOME)/zephyrproject/.venv/bin/activate && cd $(HOME)/zephyrproject && west

WCH_OPENOCD := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/openocd
WCH_CFG := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/wch-riscv.cfg
GDB := $(HOME)/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gdb
ELF := $(HOME)/zephyrproject/build/zephyr/zephyr.elf

.PHONY: build flash clean config debug

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