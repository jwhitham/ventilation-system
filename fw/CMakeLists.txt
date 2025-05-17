#
# Copyright (c) 2025 Jack Whitham
#
# SPDX-License-Identifier: BSD-3-Clause
#
# ventilation-system fw/CMakeLists.txt
#
# This is a sample project for the wifi_settings library at
# https://github.com/jwhitham/pico-wifi-settings
#

add_executable(main
        main.c
        leds.c
        temperature.c
        )
target_include_directories(main PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        )
target_link_libraries(main
        pico_cyw43_arch_lwip_threadsafe_background
        wifi_settings
        pico_stdlib
        hardware_adc
        hardware_pio
        )
target_compile_definitions(main PRIVATE
        CYW43_PIO_CLOCK_DIV_DYNAMIC=1
        )
pico_generate_pio_header(main
        ${CMAKE_CURRENT_LIST_DIR}/leds.pio
        )

pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)
pico_set_program_name(main "ventilation system control firmware")
pico_set_program_url(main "https://github.com/jwhitham/ventilation-system/")

target_compile_options(main PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

pico_add_extra_outputs(main)
