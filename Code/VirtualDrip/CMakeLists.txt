cmake_minimum_required(VERSION 3.16)

project(VirtualVDP
    VERSION 0.1.0
    DESCRIPTION "A minimal virtual TMS9918 display prototype"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(LibVNCServer REQUIRED)

# ----------------------------------------------------------------------------
# Backends
#
# The two backend libraries live under backends/ as self-contained CMake
# projects. We build them as part of this build via add_subdirectory(), which
# guarantees they are configured and compiled before virtual-vdp is linked.
# Their resulting shared libraries are collected into a single externals
# directory so the virtual-vdp executable can locate them both at link and
# run time.
# ----------------------------------------------------------------------------
set(VIRTUAL_VDP_EXTERNALS_DIR "${CMAKE_BINARY_DIR}/externals"
    CACHE PATH "Directory where the built backend libraries are collected")

add_subdirectory(backends/vDrip9958)

# Route every backend library into the shared externals directory.
set(VIRTUAL_VDP_BACKEND_TARGETS
    vdrip9958
)
set_target_properties(${VIRTUAL_VDP_BACKEND_TARGETS} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${VIRTUAL_VDP_EXTERNALS_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${VIRTUAL_VDP_EXTERNALS_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${VIRTUAL_VDP_EXTERNALS_DIR}"
)

# ----------------------------------------------------------------------------
# Main executable
# ----------------------------------------------------------------------------
add_executable(virtual-vdp
    src/app_config.c
    src/app_runtime.c
    src/main.c
    src/display_libvncserver.c
    src/input_keyboard.c
    src/keyboard_transport.c
    src/packet_dispatch.c
    src/packet_parser.c
    src/packet_replay.c
    src/protocol.c
    src/protocol_debug.c
    src/pty_console.c
    src/serial_port.c
    src/serial_reader.c
    src/storage_backend.c
    src/storage_protocol.c
    src/video_device.c
    src/video_device_vdrip9958.c
    src/virtual_text_cursor.c
    src/test_protocol.c
)

# Linking the backend targets pulls in their INTERFACE include directories
# (the headers vrEmuTms9918.h / vdrip_vdp.h) and makes virtual-vdp depend on
# them, so the libraries are always built first.
target_link_libraries(virtual-vdp
    PRIVATE
        vdrip9958
        LibVNCServer::LibVNCServer
        Threads::Threads
)

# Ensure the executable finds the collected shared libraries at run time.
set_target_properties(virtual-vdp PROPERTIES
    BUILD_RPATH "${VIRTUAL_VDP_EXTERNALS_DIR}"
)

if(MSVC)
    target_compile_options(virtual-vdp PRIVATE /W4)
else()
    target_compile_options(virtual-vdp PRIVATE -Wall -Wextra -Wpedantic)
endif()
