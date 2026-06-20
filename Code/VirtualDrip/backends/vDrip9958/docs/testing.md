# Testing

vDrip9958 uses small, deterministic C smoke tests registered with CTest. This is
a deliberate scope choice for a homebrew emulator: the tests cover representative
behavior of each subsystem rather than exhaustive mode/command matrices or
golden images.

## Running

```sh
cmake -S backends/vDrip9958 -B backends/vDrip9958/build
cmake --build backends/vDrip9958/build
ctest --test-dir backends/vDrip9958/build --output-on-failure
```

## Suites

| CTest case | Source | Scope |
|---|---|---|
| `core_public` | `tests/test_core.c` | Lifecycle, null safety, VRAM read-ahead, instance independence, reset baseline, invalid-mode geometry, palette/backdrop, status identity/selection, direct-vs-indirect, inert seams (when first built). |
| `core_internal` | `tests/test_core_internal.c` | White-box: separate state/VRAM ownership, register masks, 17-bit boundary addressing, reset state. |
| `render` | `tests/test_render.c` | Graphic 4 palette pixels, Graphic 7 direct/YJK, mode widths, interlace metadata, sprite modes 1/2 + collision, final-line + repeated-line status. |
| `commands` | `tests/test_commands.c` | Start/step/complete/STOP, HMMC CPU input + TR, LMCM S#7/TR, LMMV, transparent op, LINE, SRCH, PSET, POINT, CMD-expanded. |
| `project_check` | `tools/check_project.sh` | Static naming/artifact/dependency/required-file checks. |

## Deliberate omissions

- No golden-image / pixel-exact reference comparison.
- No exhaustive per-mode or per-command matrix.
- No cycle-timing tests (timing is functional).

Areas marked "manually reviewed" in the support matrices have no dedicated
automated case; see [deviations.md](deviations.md).

## Correctness oracle

The Yamaha V9938/V9958 manuals (`docs/*.pdf`) are the authoritative reference.
The original vDrip9928 / vrEmuTms9918 behavior is used only as a comparison
oracle for documented legacy compatibility.

## Adding a focused test

Add a `tests/test_*.c` returning non-zero on failure, then register it in
`tests/CMakeLists.txt` with `add_executable` + `target_link_libraries(... vdrip9958)`
+ `add_test`. Keep tests deterministic, isolated, and free of Virtual Drip,
network, serial, and hardware dependencies. White-box tests may include
`vDrip9958_internal.h`.
