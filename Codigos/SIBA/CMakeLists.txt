cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(SIBA)

target_sources(app PRIVATE
    src/Vcore.c
    lib/OS/OSSI.c
    lib/OS/TCKEC.c
    lib/OS/NCI.c
    lib/OS/HAL.c    
)

target_include_directories(app PRIVATE
    lib/OS
    lib/Application
    lib/SYS
)