cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SIBA)

# Driver ADC out-of-tree — inclui diretamente sem módulo
zephyr_library_named(adc_ch32v307)
zephyr_library_sources(drivers/adc/adc_ch32v307.c)
zephyr_library_link_libraries(zephyr_interface)

target_sources(app PRIVATE
    src/main.c

    # OS
    lib/OS/OSSI.c
    lib/OS/TCKEC.c
    lib/OS/NCI.c
    lib/OS/HAL.c
    lib/OS/CMD_Core.c

    # SYS
    lib/SYS/System_Manager.c
    lib/SYS/Battery_Manager.c
    lib/SYS/Security_Manager.c
    lib/SYS/Log_Manager.c
)

target_include_directories(app PRIVATE
    lib/OS
    lib/Application
    lib/SYS
    drivers
)