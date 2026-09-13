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
target_sources(app PRIVATE
    src/main.c
    src/phy_pll3_preinit.c

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
    lib/SYS/NVM.c
)

if(NOT CONFIG_ARCH_POSIX)
    # Driver ADC out-of-tree — adicionado direto em `app` (linkado com
    # --whole-archive) em vez de zephyr_library_named(): uma lib estática
    # separada não entrava na linha de link de zephyr_pre0.elf, então o
    # linker descartava adc_ch32v307.c por completo e `adc1` acabava
    # resolvendo para o driver upstream adc_ch32v00x.c (também
    # DT_DRV_COMPAT wch_adc, compilado incondicionalmente) — síncrono-only,
    # sem .read_async, causando jalr por ponteiro NULL em
    # HAL_thread_ADC_fn (ver CONFIG_ADC_CH32V00X=n em prj.conf).
    target_sources(app PRIVATE drivers/adc/adc_ch32v307.c)

    # ADC da SIBA mede sinais lentos (v_batt, i_batt, temperatura, Pitot):
    # usa o maior tempo de amostragem possível (SMP=7 = 239,5 ciclos) e
    # BATCH_FRAMES=1 para que o callback do HAL receba exatamente 1 frame
    # (nch = 3×1 = 3 = HAL_ADC_CHANNELS_PER_ADC), mantendo o loop de
    # acumulação correto sem alterar HAL.c. ISR ~24 kHz vs ~150 kHz antes.
    target_compile_definitions(app PRIVATE
        CH32_ADC_SMP=7
        CH32_ADC_BATCH_FRAMES=1
    )
endif()

target_include_directories(app PRIVATE
    lib/OS
    lib/Application
    lib/SYS
    drivers
)