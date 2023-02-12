cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(matrix_display C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()


# Initialize the SDK
pico_sdk_init()

# Include freertos
set(PICO_FREERTOS_PATH $ENV{PICO_FREERTOS_PATH})
include(${PICO_FREERTOS_PATH}/CMakeLists.txt)

# Add executable
add_executable(matrix_display
        main.c
        led_matrix.c
        led_matrix_frame_buffer.c
        tev/tev.c
        tev/list.c
        tev/heap/heap.c
        tev/map/map.c
        tev_irq_injector/tev_irq_injector.c
        )

target_include_directories(matrix_display PRIVATE 
        .
        tev 
        tev_irq_injector 
        tev/map)

# pull in common dependencies
target_link_libraries(matrix_display PUBLIC
        pico_stdlib 
        pico_multicore
        freertos
        )

# create map/bin/hex file etc.
pico_add_extra_outputs(matrix_display)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

add_compile_definitions(USE_SIMPLE_HASH)
