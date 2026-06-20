cmake_minimum_required(VERSION 3.12)

project(vDrip9958 C)

# Portable C11, no language extensions required.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Shared by default; static when configured with -DBUILD_SHARED_LIBS=OFF.
option(BUILD_SHARED_LIBS "Build vDrip9958 as a shared library" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

if (NOT BUILD_SHARED_LIBS)
  add_definitions(-DVDRIP9958_STATIC)
endif()

# Portable warning defaults only. No architecture-specific tuning
# (e.g. -march=native) and no warnings-as-errors: this is a homebrew
# library that must build cleanly across hosts and compilers.
if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)

add_subdirectory(src)

if (BUILD_TESTING)
  add_subdirectory(tests)
endif()
