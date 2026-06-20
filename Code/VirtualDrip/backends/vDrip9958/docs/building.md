# Building

vDrip9958 is a standalone CMake project (C11) depending only on the C standard
library. It builds with GCC or Clang and is verified against both.

## Prerequisites

- CMake ≥ 3.12
- A C11 compiler (GCC or Clang)

## Shared library (default)

```sh
cmake -S backends/vDrip9958 -B backends/vDrip9958/build
cmake --build backends/vDrip9958/build
ctest --test-dir backends/vDrip9958/build --output-on-failure
```

## Static library

```sh
cmake -S backends/vDrip9958 -B backends/vDrip9958/build-static -DBUILD_SHARED_LIBS=OFF
cmake --build backends/vDrip9958/build-static
```

## Selecting a compiler

```sh
cmake -S backends/vDrip9958 -B build-gcc   -DCMAKE_C_COMPILER=gcc
cmake -S backends/vDrip9958 -B build-clang -DCMAKE_C_COMPILER=clang
```

## Targets and outputs

| Target | Output |
|---|---|
| `vdrip9958` | `libvdrip9958.so` (shared) or `libvdrip9958.a` (static) |
| `test_core`, `test_core_internal`, `test_render`, `test_commands` | CTest executables in `build/bin/` |

## Compiler flags

Portable warnings only: `-Wall -Wextra -Wpedantic` (MSVC: `/W4`). No
warnings-as-errors, no architecture-specific tuning, no sanitizers imposed. The
public include path is the `src/` directory; only `vDrip9958.h` is public API.

## Static project check

```sh
backends/vDrip9958/tools/check_project.sh
```

Verifies naming, copied-artifact, dependency, and required-file rules without
building.
