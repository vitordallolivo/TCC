SHELL := /bin/bash
BOARD ?= same_board
GIT_DIR := $(shell git rev-parse --show-toplevel)
PROJECT_DIR := $(GIT_DIR)/Codigos/SAME
BUILD_DIR := $(PROJECT_DIR)/build_$(BOARD)
NATIVE_SIM_DIR := $(PROJECT_DIR)/build_native_sim
WEST = source $(HOME)/zephyrproject/.venv/bin/activate && cd $(HOME)/zephyrproject && west

WCH_OPENOCD := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/openocd
WCH_CFG := $(HOME)/.platformio/packages/tool-openocd-riscv-wch/bin/wch-riscv.cfg
GDB := $(HOME)/zephyr-sdk-1.0.1/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gdb
ELF := $(BUILD_DIR)/zephyr/zephyr.elf

.PHONY: build flash clean menuconfig debug native_sim native_sim_run native_sim_clean

build:
	$(WEST) build -b $(BOARD) $(PROJECT_DIR) -d $(BUILD_DIR) -- -DBOARD_ROOT=$(PROJECT_DIR)

flash:
	$(WCH_OPENOCD) -f $(WCH_CFG) \
		-c "init" \
		-c "halt" \
		-c "program $(BUILD_DIR)/zephyr/zephyr.bin 0x00000000 verify reset" \
		-c "shutdown"

clean:
	$(WEST) build -b $(BOARD) $(PROJECT_DIR) -d $(BUILD_DIR) --pristine -- -DBOARD_ROOT=$(PROJECT_DIR)

menuconfig:
	$(WEST) build -b $(BOARD) $(PROJECT_DIR) -d $(BUILD_DIR) -t menuconfig -- -DBOARD_ROOT=$(PROJECT_DIR)

native_sim:
	$(WEST) build -b native_sim $(PROJECT_DIR) -d $(NATIVE_SIM_DIR) -- -DBOARD_ROOT=$(PROJECT_DIR)

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