cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == BLOCO PADRÃO DO VS CODE (NÃO REMOVER) ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.2.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.2.0-a4)

set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ==========================================

# Define placa
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Importa SDK (agora sem usar variável de ambiente)
include(pico_sdk_import.cmake)

project(lampada_bitdoglab C CXX ASM)

# Inicializa SDK
pico_sdk_init()

# ── Fontes do projeto ──────────────────────────────────────────────────
add_executable(lampada_bitdoglab
    Projeto_Final.c
    aes.c
    oled.c
    led_matrix.c
    buttons.c
    joystick.c
    tuya.c
    ${PICO_MBEDTLS_PATH}/library/sha3.c
)

# ── Header do PIO ──────────────────────────────────────────────────────
pico_generate_pio_header(lampada_bitdoglab
    ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio
)

# ── Includes ───────────────────────────────────────────────────────────
target_include_directories(lampada_bitdoglab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PICO_MBEDTLS_PATH}/include/mbedtls
)

# ── Bibliotecas ────────────────────────────────────────────────────────
target_link_libraries(lampada_bitdoglab
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_mbedtls_headers
    pico_mbedtls_crypto
    hardware_i2c
    hardware_adc
    hardware_pio
    hardware_gpio
    hardware_timer
)

# ── STDIO ──────────────────────────────────────────────────────────────
pico_enable_stdio_usb(lampada_bitdoglab 1)
pico_enable_stdio_uart(lampada_bitdoglab 0)

# ── Outputs ────────────────────────────────────────────────────────────
pico_add_extra_outputs(lampada_bitdoglab)
