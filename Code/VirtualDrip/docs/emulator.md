# vDrip9958 — Standalone V9958 Emulator

The `vDrip9958` library (`backends/vDrip9958/`) is a standalone C11 emulator
of the Yamaha V9958 Video Display Processor. It has no dependency on Virtual
Drip, serial I/O, VNC, or packet protocols.

## Public API

Defined in `backends/vDrip9958/src/vDrip9958.h`. All emulator state is opaque
(forward-declared `VDrip9958`). Callers must serialize access to a single
instance.

### Lifecycle

| Function | Description |
|---|---|
| `vDrip9958New()` | Allocate instance + 128 KiB VRAM, apply reset state. Returns NULL on failure. |
| `vDrip9958Reset(vdp)` | Reset to documented power-on state. Preserves allocation. |
| `vDrip9958Destroy(vdp)` | Free VRAM and instance. NULL-safe. |

### CPU-Visible Ports

| Function | Port | Description |
|---|---|---|
| `vDrip9958WriteData(vdp, value)` | 0 | VRAM data write |
| `vDrip9958ReadData(vdp)` | 0 | VRAM data read (returns 0 on NULL) |
| `vDrip9958WriteControl(vdp, value)` | 1 | Control write (address latch / register write) |
| `vDrip9958ReadStatus(vdp)` | 1 | Status read (register selected by R#15) |
| `vDrip9958WritePalette(vdp, value)` | 2 | Palette write (indexed by R#16, auto-increments) |
| `vDrip9958WriteRegisterIndirect(vdp, value)` | 3 | Indirect register write (target from R#17) |

### Rendering

```c
void vDrip9958ScanLine(VDrip9958* vdp, uint16_t y, uint32_t* pixels);
```

Renders one native scanline. `pixels` must hold at least `VDRIP9958_MAX_WIDTH`
(512) entries. Each pixel is `0x00RRGGBB`. NULL instance or buffer → no-op.

### Display Metadata

```c
VDrip9958DisplayInfo vDrip9958GetDisplayInfo(const VDrip9958* vdp);
```

Returns current mode, width, height, interlace flag, and field number.
Width is 256 or 512; height is 192/212 (non-interlaced) or 384/424
(interlaced). Mode is `VDRIP9958_MODE_INVALID` for reserved combinations.

### Command Engine

```c
bool vDrip9958StepCommand(VDrip9958* vdp);
```

Advances the active VRAM command by one logical step. Returns `true` if a
command remains active (CE set). The command engine is fully implemented
(Unit 3).

## Internal Architecture

Three private implementation groups behind `vDrip9958_internal.h`:

| Component | Source | Responsibility |
|---|---|---|
| Core / Ports / State | `vDrip9958.c` | Instance state, registers, VRAM, port logic, display decode, reset |
| Renderer | `vDrip9958_render.c` | Per-scanline rendering for all 10 modes, sprites, palette |
| Command Engine | `vDrip9958_commands.c` | Drawing/transfer commands, CE/TR progression |

## Supported Display Modes

| Mode | Width | M-bits | Format |
|---|---|---|---|
| Text 1 | 256 | 0x01 | PF_TEXT |
| Text 2 | 512 | 0x09 | PF_TEXT |
| Multicolor | 256 | 0x02 | PF_MULTICOLOR |
| Graphic 1 | 256 | 0x00 | PF_PATTERN |
| Graphic 2 | 256 | 0x04 | PF_PATTERN |
| Graphic 3 | 256 | 0x08 | PF_PATTERN |
| Graphic 4 | 256 | 0x0C | PF_BPP4 |
| Graphic 5 | 512 | 0x10 | PF_BPP2 |
| Graphic 6 | 512 | 0x14 | PF_BPP4 |
| Graphic 7 | 256 | 0x1C | PF_DIRECT |

Mode bits: M1=R#1D4, M2=R#1D3, M3=R#0D1, M4=R#0D2, M5=R#0D3.
BL (blank) is R#1D6 (0x40 = unblanked).
Interlace: R#9D3. Line count: R#9D7 (0=192, 1=212).

## Reset State

At `vDrip9958New()` or `vDrip9958Reset()`:
- All control registers = 0
- Status S#1 = V9958 identification
- Programmable palette = MSX2 standard 16-color set
- All 128 KiB VRAM = 0
- Display decoded from zeroed registers (Graphic 1, blanked)

## Intentional Omissions

- No cycle accuracy or `/WAIT` timing
- No analog video output, color burst, or external video bus
- No mouse, light pen, or external interrupt sources
- VRAM is a flat byte array; no DRAM refresh modeling
- Command stepping is functional-step-based, not micro-operation timed

---

*Derived from `backends/vDrip9958/src/vDrip9958.h`, `vDrip9958.c`, `vDrip9958_render.c`, `vDrip9958_commands.c`. Verified 2026-06-20.*
