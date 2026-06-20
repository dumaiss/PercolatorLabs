# Verification Record

Final verification evidence for the standalone vDrip9958 emulator.

## Environment

- Date: 2026-06-20
- Platform: Linux
- CMake: 4.3.4
- GCC: 15.2.0 (Ubuntu)
- Clang: 21.1.8 (Ubuntu)
- Dependencies: C standard library only

## Build and test commands

```sh
# GCC (shared)
cmake -S backends/vDrip9958 -B build-gcc -DCMAKE_C_COMPILER=gcc
cmake --build build-gcc
ctest --test-dir build-gcc --output-on-failure

# Clang (shared)
cmake -S backends/vDrip9958 -B build-clang -DCMAKE_C_COMPILER=clang
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure

# Static configuration (GCC and Clang)
cmake -S backends/vDrip9958 -B build-static -DBUILD_SHARED_LIBS=OFF
cmake --build build-static
```

## Results

| Configuration | Compiler | Warnings | CTest |
|---|---|---|---|
| Shared | GCC 15.2.0 | 0 | 5/5 passed |
| Static | GCC 15.2.0 | 0 | 5/5 passed |
| Shared | Clang 21.1.8 | 0 | 5/5 passed |
| Static | Clang 21.1.8 | 0 | 5/5 passed |

Compile flags: `-Wall -Wextra -Wpedantic`, C11. No warnings in any
configuration.

### CTest cases

| Case | Result |
|---|---|
| `core_public` | passed |
| `core_internal` | passed |
| `render` | passed |
| `commands` | passed |
| `project_check` | passed |

## Smoke coverage summary

- Core: lifecycle, null safety, VRAM read-ahead, instance independence, reset
  baseline, invalid-mode geometry, register masks, 17-bit boundary, status
  identity/selection, palette commit.
- Rendering: Graphic 4 palette pixels, Graphic 7 direct + YJK, mode widths,
  interlace metadata, sprite modes 1/2 + collision, final/repeated-line status.
- Commands: start/step/complete/STOP, HMMC CPU input + TR, LMCM S#7/TR, LMMV,
  transparent op, LINE, SRCH, PSET, POINT, CMD-expanded.

## Naming-sweep exceptions

The legacy-name search (`9928`, `tms9918`, `vrEmuTms9918`, `VR_EMU_TMS9918`,
`VR_TMS9918`) over active source/build/test files returns only attribution text:

| Match | Location | Category | Reason |
|---|---|---|---|
| `vDrip9928` | `src/vDrip9958.h` (header comment) | Attribution / starting-point history | Identifies the copied origin; not the current identity. |
| `vrEmuTms9918` | `src/vDrip9958.h` (header comment) | MIT attribution | Required upstream credit under the MIT license. |

No active identifier, include guard, macro, CMake name, or current-behavior
comment uses a stale name. No stale filenames exist.

## Scope, dependency, and license checks

- Changes confined to `backends/vDrip9958` plus AI-DLC documents.
- `backends/vDrip9928` and Virtual Drip host/proxy integration unchanged.
- No host / network / serial / RFB / Python / vDrip9928 dependency in active
  sources; tests need no external service or hardware.
- `LICENSE` (MIT) retained; upstream attribution preserved in source headers.
- `tools/check_project.sh` passes (registered as the `project_check` CTest case).

## Remaining manual-validation risk

The behaviors listed as "Implemented + manually reviewed" in the support
matrices and in [deviations.md](deviations.md) have no dedicated automated case
and should be validated against the Yamaha manuals before relying on
pixel/command exactness. The notable items: bitmap page-base and Graphic 2/3
banking, sprite 16×16 ordering / mode-2 color offset / Graphic 5 tiling,
two-page scroll wrapping (not implemented), R#18 display adjustment (not
applied), command overlap exactness, the partial post-command register table,
and ARG/SRCH detail. None of these block the standalone library's required
functionality; they are accuracy refinements.
