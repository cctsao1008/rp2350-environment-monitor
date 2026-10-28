cmake_minimum_required(VERSION 3.21)

set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "Waveshare RP2350 PiZero board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "RP2350 Arm secure platform")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk" CACHE PATH "Pinned Pico SDK path")

if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "Pico SDK missing. Run: git submodule update --init --recursive")
endif()

# Explicit package checks prevent the SDK from silently downloading/building
# host-side tools when a compatible installation cannot be found.
find_package(picotool 2.3.0 REQUIRED CONFIG NO_CMAKE_FIND_ROOT_PATH)
find_package(pioasm 2.3.1 REQUIRED CONFIG NO_CMAKE_FIND_ROOT_PATH)

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(rp2350_environment_monitor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if(NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.3.1")
    message(FATAL_ERROR "Expected Pico SDK 2.3.1, found ${PICO_SDK_VERSION_STRING}")
endif()

add_executable(environment_monitor
    src/main.c
    src/sensor_manager.c
    src/sense_hat_led.c
    src/sensors/bme280_port.c
    src/sensors/sensor_i2c_util.c
    src/sensors/tsl2591.c
    src/sensors/ltr390.c
    src/sensors/sgp40.c
    src/sensors/icm20948.c
    third_party/bosch_bme280/bme280.c
    third_party/sensirion_gas_index/sensirion_gas_index_algorithm.c
)

target_include_directories(environment_monitor PRIVATE
    src
    third_party/bosch_bme280
    third_party/sensirion_gas_index
)

target_link_libraries(environment_monitor PRIVATE
    pico_stdlib
    hardware_i2c
    m
)

target_compile_definitions(environment_monitor PRIVATE
    # When the SDK vendor-reset interface enters BOOTSEL, disable MSC only.
    # Physical BOOTSEL at cold boot is controlled by ROM, not this setting.
    PICO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK=1
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    BME280_DOUBLE_ENABLE=1
)

pico_enable_stdio_usb(environment_monitor 1)
pico_enable_stdio_uart(environment_monitor 0)

# ELF is the canonical flashing image. Other generated formats are NOT
# used by the firmware-update procedure.
pico_add_extra_outputs(environment_monitor)
