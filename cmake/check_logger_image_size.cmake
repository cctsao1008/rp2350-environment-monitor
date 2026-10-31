if(NOT DEFINED IMAGE OR NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "Logger firmware image guard cannot find generated binary: ${IMAGE}")
endif()
if(NOT DEFINED MAX_BYTES OR NOT MAX_BYTES EQUAL 4194304)
    message(FATAL_ERROR "Unexpected binary logger partition boundary")
endif()
file(SIZE "${IMAGE}" _size)
if(_size GREATER MAX_BYTES)
    message(FATAL_ERROR
        "DANGER: firmware image is ${_size} bytes and overlaps logger partition "
        "beginning at ${MAX_BYTES}. Refusing to build a flashable ELF.")
endif()
message(STATUS "Logger partition guard PASS: firmware ${_size} bytes < 4 MiB")
