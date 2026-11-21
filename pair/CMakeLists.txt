cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(${CMAKE_CURRENT_LIST_DIR}/../matrix/pico_sdk_import.cmake)

project(pair C CXX ASM)

pico_sdk_init()

add_executable(pair main.cpp)

pico_set_program_name(pair "pair")
pico_enable_stdio_usb(pair 1)
pico_enable_stdio_uart(pair 0)

target_compile_definitions(pair PRIVATE
    PICO_STDIO_USB_CONNECT_TIMEOUT_MS=0
)

target_link_libraries(pair
    pico_stdlib
    hardware_i2c
    hardware_gpio
)

pico_add_extra_outputs(pair)
