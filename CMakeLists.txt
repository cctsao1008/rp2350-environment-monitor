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

# Default setup: Raspberry Pi Sense HAT V1 physically removed.
# OFF excludes all Sense HAT sources and avoids probing those I2C addresses.
option(ENABLE_SENSE_HAT_V1 "Build optional Raspberry Pi Sense HAT V1 drivers and console" OFF)
# Experimental branch-only USB composite networking. OFF keeps the
# original CDC + picotool path, allowing non-invasive A/B testing.
option(ENABLE_USB_NCM_WEB_PHASE1 "Experimental CDC-NCM + CDC + vendor reset" OFF)
if(ENABLE_USB_NCM_WEB_PHASE1 AND ENABLE_SENSE_HAT_V1)
    message(FATAL_ERROR "Experimental USB-NCM mode currently supports Environment HAT only")
endif()


if(NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.3.1")
    message(FATAL_ERROR "Expected Pico SDK 2.3.1, found ${PICO_SDK_VERSION_STRING}")
endif()

add_executable(environment_monitor
    src/main.c
    src/sensor_manager.c
    src/sensors/bme280_port.c
    src/sensors/sensor_i2c_util.c
    src/sensors/tsl2591.c
    src/sensors/ltr390.c
    src/sensors/sgp40.c
    src/sensors/icm20948.c
    third_party/bosch_bme280/bme280.c
    third_party/sensirion_gas_index/sensirion_gas_index_algorithm.c
)

if(ENABLE_SENSE_HAT_V1)
    target_sources(environment_monitor PRIVATE
        src/sense_hat_led.c
        src/sense_hat_marquee.c
        src/sense_hat_console.c
        src/sense_hat_probe.c
        src/sense_hat_sensors.c
    )
endif()

if(ENABLE_USB_NCM_WEB_PHASE1)
    # Use the pinned SDK's TinyUSB + lwIP submodules; no FetchContent.
    foreach(_dep
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c"
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dnserver.c"
        "${PICO_SDK_PATH}/lib/lwip/src/include/lwip/init.h")
        if(NOT EXISTS "${_dep}")
            message(FATAL_ERROR "USB NCM dependency missing: ${_dep}. Run git submodule update --init --recursive")
        endif()
    endforeach()
    target_sources(environment_monitor PRIVATE
        src/usb_composite/usb_descriptors.c
        src/usb_composite/usb_ncm_network.c
        src/usb_composite/usb_cdc_stdio.c
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c"
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dnserver.c"
    )
    target_include_directories(environment_monitor PRIVATE
        src/usb_composite
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking"
    )
    target_link_libraries(environment_monitor PRIVATE
        tinyusb_device
        pico_usb_reset
        pico_lwip_core
        pico_lwip_core4
        pico_lwip_netif
        pico_lwip_arch
        pico_lwip_http
    )
    target_compile_definitions(environment_monitor PRIVATE
        # Application provides one merged WINNCM + WINUSB descriptor set.
        PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
        PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
        PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
    )
endif()

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
    PROJECT_ENABLE_USB_WEB_PHASE1=$<BOOL:${ENABLE_USB_NCM_WEB_PHASE1}>
    PROJECT_ENABLE_SENSE_HAT_V1=$<BOOL:${ENABLE_SENSE_HAT_V1}>
)

if(ENABLE_USB_NCM_WEB_PHASE1)
    # pico_stdio_usb cannot own TinyUSB concurrently with our composite stack.
    pico_enable_stdio_usb(environment_monitor 0)
else()
    pico_enable_stdio_usb(environment_monitor 1)
endif()
pico_enable_stdio_uart(environment_monitor 0)

# ELF is the canonical flashing image. Other generated formats are NOT
# used by the firmware-update procedure.
pico_add_extra_outputs(environment_monitor)
