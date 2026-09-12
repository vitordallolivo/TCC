cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SAME)

# SAME — nó de aquisição ELÉTRICA do motor. Só LÊ (2 ADCs) e estima ω/torque
# via SMO+PLL; não aciona o motor (quem aciona o ESC é a SIBA). Arquitetura
# em camadas espelha a SIBA (OS/SYS/HAL, despacho por eventos via same_wq).
#
# native_sim / POSIX: mesma estratégia da SIBA — os dois ADCs viram
# zephyr,adc-emul sob os MESMOS nodelabels `adc1`/`adc2` (ver
# boards/native_sim.overlay), então HAL.c roda sem mudança nesse subsistema.
# NCI.c usa os Offloaded sockets (NSOS) do native_sim → troca UDP real com a
# Aplicacao_Pi no localhost. Ver boards/native_sim.conf.
target_sources(app PRIVATE
    src/main.c

    # OS
    lib/OS/OSSI.c
    lib/OS/NCI.c
    lib/OS/HAL.c
    lib/OS/CMD_Core.c

    # SYS
    lib/SYS/Log_Manager.c
    lib/SYS/Observer_Manager.c
    lib/SYS/Security_Manager.c
    lib/SYS/NVM.c
)

if(NOT CONFIG_ARCH_POSIX)
    # Driver ADC out-of-tree (ADC1 + ADC2) — linkado direto em `app`
    # (--whole-archive) pelo mesmo motivo da SIBA: uma lib estática separada
    # seria descartada pelo linker e `adc1`/`adc2` resolveriam para o driver
    # upstream síncrono-only. Ver CONFIG_ADC_CH32V00X=n em prj.conf.
    target_sources(app PRIVATE drivers/adc/adc_ch32v307.c)
endif()

target_include_directories(app PRIVATE
    lib/OS
    lib/Application
    lib/SYS
    drivers
)
