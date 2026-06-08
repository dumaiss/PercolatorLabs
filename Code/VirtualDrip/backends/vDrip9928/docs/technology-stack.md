# Technology Stack

## Languages

| Language | Standard | Use |
|----------|----------|-----|
| **C** | C99 | Core emulator, utility library |
| **C++** | C++11 | Python bindings (pybind11 requires C++11) |
| **Python** | 3.x | Test harness, integration scripts |
| **CMake** | 3.12+ | Build system |

## Build System

### Primary: CMake

**Root `CMakeLists.txt`**:
- Requires CMake ≥ 3.12
- Sets C standard to C11
- Option `BUILD_SHARED_LIBS` (ON by default)
- Compiler flags: `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang), `/W4 /WX` (MSVC)
- Native optimization: `-march=native` (Linux, non-cross-compile)
- Includes CTest for test discovery

**`src/CMakeLists.txt`**:
- Builds two targets: `vrEmuTms9918` and `vrEmuTms9918Util`
- `vrEmuTms9918Util` links publicly against `vrEmuTms9918`
- Exports `vrEmuTms9918` include directory as INTERFACE

### Secondary: Standalone Makefile (`pybindings/Makefile`)

Hand-written Makefile for building the Python extension module:
- Compiles `vrEmuTms9918.c` and `vrEmuTms9918Util.c` with `-D VR_TMS9918_EMU_STATIC` (static linking mode)
- Links with `g++ -shared -fPIC` using pybind11 includes
- Output: `tms9918.<platform-suffix>.so`

## Platform Support

| Platform | Compilers | Status |
|----------|-----------|--------|
| Linux (x86_64) | GCC, Clang | ✅ Primary — built |
| Windows | MSVC | ✅ CI-tested |
| WebAssembly (Emscripten) | emcc | ✅ Supported (has `__EMSCRIPTEN__` linkage macros) |
| Raspberry Pi Pico | Pico SDK (ARM GCC) | ✅ Used in PICO9918 project |

## CI / CD

GitHub Actions workflow (`.github/workflows/cmake-multi-platform.yml`):
- **Triggers**: Push/PR to `main`
- **Matrix**: Ubuntu × {gcc, clang}, Windows × {MSVC}
- **Steps**: Configure → Build → Test (CTest)
- **Strategy**: `fail-fast: false`

## Key Compiler Defines

| Define | Purpose |
|--------|---------|
| `VR_TMS9918_EMU_COMPILING_DLL` | Building as DLL (Windows) — enables `__declspec(dllexport)` |
| `VR_EMU_TMS9918_STATIC` | Static linking — disables dllimport/dllexport |
| `PICO_BUILD` | Raspberry Pi Pico target — includes pico/stdlib.h, changes inline semantics |
| `__EMSCRIPTEN__` | WebAssembly target — uses `EMSCRIPTEN_KEEPALIVE` |
| `WIN32` | Windows — triggers different DLL linkage macros |

## External Dependencies

**Core library**: **None** — only standard C headers (`stdlib.h`, `memory.h`, `math.h`, `string.h`).

**Python bindings**: **pybind11** (header-only C++ library, installed via system package or pip).

**Test harness**: **Pillow (PIL)** for image display.
