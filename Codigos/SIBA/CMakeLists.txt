cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SIBA)

# Modo native_sim / POSIX (ver CONFIG_ARCH_POSIX): a placa real (CH32V307 +
# driver ADC custom + GPIO bit-bang + PWM/TIM4 + TIM3 + Ethernet MAC) não
# existe nesse alvo — mas em vez de manter um HAL paralelo (HAL_mock.c,
# removido), HAL.c roda nos dois alvos: ADC usa o emulador in-tree
# zephyr,adc-emul sob o MESMO nodelabel `adc1` (ver native_sim.overlay/
# native_sim.conf — API genérica adc_channel_setup/adc_read_async, sem
# alteração); ESC/RPM/HX711 (PWM, TIM3 raw-MMIO, GPIO bit-bang — sem
# substituto in-tree viável fora do framework de teste FFF) ganham pequenos
# blocos `#ifdef CONFIG_ARCH_POSIX` sintéticos isolados dentro do próprio
# HAL.c. NCI.c continua o mesmo nos dois alvos: em native_sim,
# boards/native_sim.conf liga os "Offloaded sockets" (NSOS), que repassam
# zsock_socket/bind/sendto direto para os sockets BSD do host (127.0.0.1) —
# então o firmware troca pacotes UDP de verdade com Aplicacao_Pi sem
# precisar de hardware. Ver CLAUDE.md/HW.md "Mock mode (native_sim)" e
# Aplicacao_Pi/CLAUDE.md.
if(NOT CONFIG_ARCH_POSIX)
    # Driver ADC out-of-tree — inclui diretamente sem módulo
    zephyr_library_named(adc_ch32v307)
    zephyr_library_sources(drivers/adc/adc_ch32v307.c)
    zephyr_library_link_libraries(zephyr_interface)



    zephyr_library_sources_ifdef(CONFIG_ETH_WCH
        drivers/ethernet/eth_wch.c
    )
    zephyr_include_directories_ifdef(CONFIG_ETH_WCH
        ${ZEPHYR_BASE}/drivers/ethernet
    )
endif()

target_sources(app PRIVATE
    src/main.c

    # OS
    lib/OS/OSSI.c
    lib/OS/TCKEC.c
    lib/OS/NCI.c
    lib/OS/HAL.c
    lib/OS/CMD_Core.c

    # SYS
    lib/SYS/Sys.c
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