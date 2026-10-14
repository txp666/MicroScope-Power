cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_power_scope C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT DEFINED APP_VERSION)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" APP_VERSION)
    string(STRIP "${APP_VERSION}" APP_VERSION)
endif()

add_executable(pico_power_scope
    src/main.c
    src/adc_voltage.c
    src/app_settings.c
    src/board.c
    src/buzzer.c
    src/encoder.c
    src/local_ui.c
    src/oled_ssd1306.c
    src/sampler.c
    src/usb_webusb.c
    src/protocol.c
    src/ads8866.c
    src/power_output.c
)

target_include_directories(pico_power_scope PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_options(pico_power_scope PRIVATE
    -Wall
    -Wextra
)

target_compile_definitions(pico_power_scope PRIVATE
    PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=10000
    PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0
    PICO_POWER_SCOPE_VERSION="${APP_VERSION}"
)

target_link_libraries(pico_power_scope PRIVATE
    pico_stdlib
    pico_unique_id
    pico_stdio_usb
    pico_multicore
    hardware_adc
    hardware_flash
    hardware_i2c
    hardware_pwm
    hardware_spi
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(pico_power_scope 1)
pico_enable_stdio_uart(pico_power_scope 0)
pico_add_extra_outputs(pico_power_scope)
