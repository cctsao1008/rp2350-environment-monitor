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
    # Windows 11 UsbNcm requires NTB IN/OUT divisors of 4 (pinned
    # TinyUSB provides 1). Generate a replacement source in the build
    # directory from the pinned Git object, never from dirty worktree data.
    set(_tinyusb_ncm_original
        "${PICO_SDK_PATH}/lib/tinyusb/src/class/net/ncm_device.c")
    if(NOT EXISTS "${_tinyusb_ncm_original}")
        message(FATAL_ERROR "Pinned TinyUSB NCM driver missing: ${_tinyusb_ncm_original}")
    endif()
    # Read the checked-out TinyUSB commit's Git object, not the local
    # working-tree file. Thus an old manual two-line change in the
    # nested submodule does not affect the generated firmware source.
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PICO_SDK_PATH}/lib/tinyusb"
            show "HEAD:src/class/net/ncm_device.c"
        OUTPUT_VARIABLE _ncm_source
        ERROR_VARIABLE _ncm_git_error
        RESULT_VARIABLE _ncm_git_result
    )
    if(NOT _ncm_git_result EQUAL 0)
        message(FATAL_ERROR "Cannot read pinned TinyUSB NCM Git object: ${_ncm_git_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_tinyusb_ncm_original}")
    # Match the assignment, not its indentation/alignment: TinyUSB uses
    # 12 spaces before InDivisor '=' but 11 spaces before OutDivisor '='.
    # Only the uniquely matched numeric value may change; any other
    # upstream definition fails closed instead of being silently modified.
    foreach(_field IN ITEMS wNdbInDivisor wNdbOutDivisor)
        string(REGEX MATCHALL
            "\\\\.${_field}[ \\t]*=[ \\t]*[0-9]+[ \\t]*,"
            _assignments "${_ncm_source}")
        list(LENGTH _assignments _assignment_count)
        if(NOT _assignment_count EQUAL 1)
            message(FATAL_ERROR
                "Expected exactly one pinned TinyUSB ${_field} assignment, got ${_assignment_count}")
        endif()
        list(GET _assignments 0 _assignment)
        if(_assignment MATCHES "=[ \\t]*1[ \\t]*,$")
            string(REGEX REPLACE "1([ \\t]*,)$" "4\\\\1"
                _corrected "${_assignment}")
            string(REPLACE "${_assignment}" "${_corrected}"
                _ncm_source "${_ncm_source}")
        elseif(NOT _assignment MATCHES "=[ \\t]*4[ \\t]*,$")
            message(FATAL_ERROR
                "Unexpected pinned TinyUSB ${_field} value in: ${_assignment}")
        endif()
    endforeach()
    set(_ncm_override "${CMAKE_CURRENT_BINARY_DIR}/generated/tinyusb_ncm_device.c")
    if(EXISTS "${_ncm_override}")
        file(READ "${_ncm_override}" _existing_ncm_override)
    else()
        set(_existing_ncm_override "")
    endif()
    if(NOT "${_existing_ncm_override}" STREQUAL "${_ncm_source}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
        file(WRITE "${_ncm_override}" "${_ncm_source}")
    endif()
    # TinyUSB's interface sources include the unmodified NCM translation unit.
    # Suppress only that TU for this executable, replacing it with the generated
    # equivalent that differs solely in the two NTB divisor assignments.
    set_source_files_properties("${_tinyusb_ncm_original}"
        PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(environment_monitor PRIVATE "${_ncm_override}")
    message(STATUS "USB NCM: using generated NTB-divisor=4 driver (SDK untouched)")

    target_sources(environment_monitor PRIVATE
        src/usb_composite/usb_descriptors.c
        src/usb_composite/usb_ncm_network.c
        src/usb_composite/sensor_web.c
        src/usb_composite/usb_cdc_stdio.c
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c"
        "${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dnserver.c"
    )
    target_include_directories(environment_monitor PRIVATE
        src/usb_composite
        # Generated NCM source is outside the vendor directory; retain its
        # original local include search path for ncm.h / net_device.h.
        "${PICO_SDK_PATH}/lib/tinyusb/src/class/net"
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
