# vDrip9958 API Reference

The public API is declared in `src/vDrip9958.h`. Only that header is supported
consumer API; `vDrip9958_internal.h` is private. The caller serializes all
access to a single instance.

All scanline output pixels are packed as `0x00RRGGBB`.

## Types

| Type | Description |
|---|---|
| `VDrip9958` | Opaque emulator instance. |
| `VDrip9958Mode` | Decoded display mode: `TEXT1`, `TEXT2`, `MULTICOLOR`, `GRAPHIC1`–`GRAPHIC7`, `INVALID`. |
| `VDrip9958DisplayInfo` | `{ uint16_t width; uint16_t height; VDrip9958Mode mode; bool interlaced; uint8_t field; }` |

Constants: `VDRIP9958_MAX_WIDTH` (512), `VDRIP9958_MAX_HEIGHT` (424).

## Lifecycle

| Function | Behavior |
|---|---|
| `VDrip9958* vDrip9958New(void)` | Allocates an instance and its own 128 KiB VRAM, reset to power-on state. Returns `NULL` on allocation failure (no leak). |
| `void vDrip9958Reset(VDrip9958*)` | Deterministic reset: zeroed undefined state, zeroed registers (incl. R#25–27), standard MSX2 palette, zeroed VRAM, GRAPHIC1 256×192 baseline. `NULL` ignored. |
| `void vDrip9958Destroy(VDrip9958*)` | Frees the instance and VRAM. `NULL` accepted. |

## CPU ports (V9958 I/O ports 0–3)

| Function | Port | Behavior |
|---|---|---|
| `void vDrip9958WriteData(VDrip9958*, uint8_t)` | 0 | VRAM data write at the current 17-bit address, then auto-increment with R#14 carry. `NULL` ignored. |
| `uint8_t vDrip9958ReadData(VDrip9958*)` | 0 | Returns the read-ahead byte, refills it from VRAM, then increments. `0` on `NULL`. |
| `void vDrip9958WriteControl(VDrip9958*, uint8_t)` | 1 | Two-write latch: address setup (read/write) or direct register write. `NULL` ignored. |
| `uint8_t vDrip9958ReadStatus(VDrip9958*)` | 1 | Returns the status register selected by R#15 (S#0–S#9), applying documented read-to-clear and S#7 command-result effects. `0` on `NULL`. |
| `void vDrip9958WritePalette(VDrip9958*, uint8_t)` | 2 | Two-byte palette entry write (R/B then G) at the index in R#16, then advances R#16. `NULL` ignored. |
| `void vDrip9958WriteRegisterIndirect(VDrip9958*, uint8_t)` | 3 | Indirect register write via R#17 (auto-increment unless inhibited; R#17 self-write rejected). `NULL` ignored. |

## Rendering

`void vDrip9958ScanLine(VDrip9958*, uint16_t y, uint32_t* pixels)`

Renders one native scanline at output line `y` into `pixels`, which must hold at
least `VDRIP9958_MAX_WIDTH` entries. Each pixel is `0x00RRGGBB`. `NULL` instance
or buffer is ignored. Out-of-range `y` fills the current width with
border/background and applies no side effects. Rendering the final output line
completes a frame (vertical interrupt when enabled, blink/field advance). The
renderer never retains the caller's buffer.

## Display metadata

`VDrip9958DisplayInfo vDrip9958GetDisplayInfo(const VDrip9958*)`

Returns a snapshot of `{width, height, mode, interlaced, field}`. Interlaced
modes report the woven output height (384/424). An invalid/reserved mode reports
`VDRIP9958_MODE_INVALID` while retaining the most recent valid geometry. On
`NULL`, returns a zeroed snapshot with mode `INVALID`.

## Command engine

`bool vDrip9958StepCommand(VDrip9958*)`

Advances an active VRAM command by one natural unit (one packed byte, pixel,
line point, search, or single point) and returns `true` while a command remains
active (CE set). Returns `false` for `NULL` or when idle. Commands are started by
writing R#46; CPU-to-VRAM transfers (HMMC/LMMC) provide data through R#44;
VRAM-to-CPU transfers (LMCM) deliver data through S#7 reads. CE/TR are observable
in S#2. See [command-support.md](command-support.md).

## Null-safety summary

Void methods return on `NULL`; byte readers return `0`; `vDrip9958StepCommand`
returns `false`; `vDrip9958GetDisplayInfo` returns an `INVALID` snapshot;
`vDrip9958Destroy(NULL)` is valid. The library contains no assertions and never
terminates the host process.
