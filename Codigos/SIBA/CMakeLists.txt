cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SIBA)

# Modo mock (native_sim / POSIX, ver CONFIG_ARCH_POSIX): a placa real
# (CH32V307 + driver ADC custom + GPIO bit-bang + PWM + Ethernet MAC) não
# existe nesse alvo, então HAL.c + adc_ch32v307 são trocados por HAL_mock.c,
# que gera dados sintéticos pelo mesmo barramento OSSI. NCI.c continua o
# mesmo nos dois alvos: em native_sim, boards/native_sim.conf liga os
# "Offloaded sockets" (NSOS), que repassam zsock_socket/bind/sendto direto
# para os sockets BSD do host (127.0.0.1) — então o firmware troca pacotes
# UDP de verdade com Aplicacao_Pi sem precisar de hardware. Ver CLAUDE.md/
# HW.md "Mock mode (native_sim)" e Aplicacao_Pi/CLAUDE.md.
if(NOT CONFIG_ARCH_POSIX)
    # Driver ADC out-of-tree — inclui diretamente sem módulo
    zephyr_library_named(adc_ch32v307)
    zephyr_library_sources(drivers/adc/adc_ch32v307.c)
    zephyr_library_link_libraries(zephyr_interface)

    set(SIBA_HAL_SOURCE lib/OS/HAL.c)
else()
    set(SIBA_HAL_SOURCE lib/OS/HAL_mock.c)
endif()

target_sources(app PRIVATE
    src/main.c

    # OS
    lib/OS/OSSI.c
    lib/OS/TCKEC.c
    lib/OS/NCI.c
    ${SIBA_HAL_SOURCE}
    lib/OS/CMD_Core.c

    # SYS
    lib/SYS/System_Manager.c
    lib/SYS/Battery_Manager.c
    lib/SYS/Propeller_Manager.c
    lib/SYS/Security_Manager.c
    lib/SYS/Log_Manager.c
    lib/SYS/Test_Shell.c
)

target_include_directories(app PRIVATE
    lib/OS
    lib/Application
    lib/SYS
    drivers
)