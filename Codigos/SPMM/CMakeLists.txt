cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SPMM)

# Aplicacao — threads diretas do Zephyr + shell nativo (SHELL_CMD_ARG_REGISTER),
# sem a camada OSSI/TCKEC/CMD da SIBA (SPMM e' provisorio: prioriza simplicidade).
target_sources(app PRIVATE
    src/main.c
    src/esc.c
    src/rpm.c
    src/acquisition.c
    src/telemetry.c
    src/shell_cmds.c
)

# Driver ADC out-of-tree — adicionado direto em `app` (linkado com
# --whole-archive) em vez de uma lib estatica separada. Uma lib separada nao
# entra na linha de link de zephyr_pre0.elf, entao o linker descarta
# adc_ch32v307.c e `adc1` acaba resolvendo para o driver upstream
# adc_ch32v00x.c (sincrono-only, sem .read_async → jalr por ponteiro NULL).
# Mesmo motivo/solucao da SIBA. Nao compila para native_sim (usa adc_emul).
if(NOT CONFIG_ARCH_POSIX)
    target_sources(app PRIVATE drivers/adc/adc_ch32v307.c)
endif()

target_include_directories(app PRIVATE src)
