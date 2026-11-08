cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Lets the Pico VS Code extension find this project.
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

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(plasma C CXX ASM)

pico_sdk_init()

set(HUB75_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/waveshare/src)

add_executable(plasma
    plasma/plasma.cpp
    ${HUB75_DIR}/hub75.cpp
    ${HUB75_DIR}/fm6126a.cpp
    ${HUB75_DIR}/rul6024.cpp
)

pico_set_program_name(plasma "plasma")
pico_set_program_version(plasma "1.0")
set(PICO_PIO_VERSION 1)
pico_set_float_implementation(plasma pico)

# Pico RAM is tight at 64x64. Later boards keep the driver's balanced scan.
if(PICO_BOARD STREQUAL "pico")
    set(BALANCED_LIGHT_OUTPUT_VALUE false)
else()
    set(BALANCED_LIGHT_OUTPUT_VALUE true)
endif()

target_compile_definitions(plasma PRIVATE
    USE_PICO_GRAPHICS=false
    MATRIX_PANEL_WIDTH=64
    MATRIX_PANEL_HEIGHT=64
    CHAIN_MODE=CHAIN_MODE_SERPENTINE
    CHAIN_COLS=1
    CHAIN_ROWS=1
    DATA_BASE_PIN=0
    DATA_N_PINS=6
    SWAP_RB_PINS=false
    ROWSEL_BASE_PIN=6
    ROWSEL_N_PINS=5
    PANEL_SCAN_DEPTH=32
    CLK_PIN=11
    STROBE_PIN=12
    OEN_PIN=13
    PANEL_TYPE=PANEL_GENERIC
    INVERTED_STB=false
    SM_CLOCKDIV_FACTOR=2.0f
    BITPLANES=8
    BALANCED_LIGHT_OUTPUT=${BALANCED_LIGHT_OUTPUT_VALUE}
    SEPARATE_CIE_CHANNELS=true
    CCM_RG_SHIFT=6
    CCM_GB_SHIFT=7
    BASE_LATCH_NS=120
    BASE_ADDR_NS=200
    BASE_OE_NS=60
    HUB75_MULTICORE=true
    FRAME_RATE=false
    HUB75
)

set_source_files_properties(${HUB75_DIR}/hub75.cpp PROPERTIES
    COMPILE_OPTIONS "-include${CMAKE_CURRENT_LIST_DIR}/hub75_compat.hpp"
)

pico_generate_pio_header(plasma ${HUB75_DIR}/hub75.pio)

pico_enable_stdio_uart(plasma 0)
pico_enable_stdio_usb(plasma 1)

target_include_directories(plasma PRIVATE ${HUB75_DIR})

target_link_libraries(plasma
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_clocks
)

pico_add_extra_outputs(plasma)
