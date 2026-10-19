cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(silentype_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(silentype
    src/main.c
    src/print_engine.c
    src/usb_descriptors.c
    src/crc8.c
    src/shiftreg.c
)
pico_generate_pio_header(silentype ${CMAKE_CURRENT_LIST_DIR}/src/silentype_sr.pio)
target_include_directories(silentype PRIVATE src)   # tusb_config.h lives here
target_link_libraries(silentype
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_pio
    tinyusb_device
    tinyusb_board
)
pico_enable_stdio_uart(silentype 1)
pico_enable_stdio_usb(silentype 0)   # USB is the CDC data pipe, not stdio
pico_add_extra_outputs(silentype)
