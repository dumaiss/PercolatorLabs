# vDrip9958 Architecture

vDrip9958 is one standalone C library built from three runtime components behind
a single public facade, plus a private state aggregate.

## Components

| Component | Source | Responsibility |
|---|---|---|
| Core / public facade | `vDrip9958.c` | Lifecycle, reset, registers, status, palette, CPU ports, 128 KiB VRAM access, display decode. Routes R#46/R#44/S#7 to the command engine and the scanline call to the renderer. |
| Renderer | `vDrip9958_render.c` | Per-call display descriptor decode, color services, coordinate/scroll mapping, all background modes, both sprite modes, status/frame commit. |
| Command engine | `vDrip9958_commands.c` | Functional execution of the VRAM command set with CE/TR progression and CPU transfers. |
| Private state + seams | `vDrip9958_internal.h` | The opaque instance, entities, constants, and the core/render/command seams. |
| Public API | `vDrip9958.h` | The only supported consumer header. |

## State and VRAM lifetime

The opaque `VDrip9958` instance owns all mutable state: register and status
files, the programmable palette, CPU-port latches/address/read-ahead, decoded
display metadata, frame-derived rendering state, and the command-engine state.
The 128 KiB display VRAM is a separate heap allocation owned by the instance.
`vDrip9958New` allocates both transactionally (no partial instance is returned);
`vDrip9958Destroy` releases both. No mutable state is global.

## Ownership

- **Core** owns registers, status, palette, latches, VRAM, and display metadata,
  and is the only writer of the CPU-port paths.
- **Renderer** reads core state and VRAM, writes only the caller's scanline
  buffer, and merges line/frame status effects back into core status. It never
  mutates VRAM.
- **Command engine** reads and writes VRAM through bounded helpers and commits
  command results into core registers/status. It never calls the renderer.

## Workflows

- **Ports**: `WriteControl` drives a two-write latch for register writes and
  VRAM address setup; data reads use a one-byte read-ahead pipeline; the 17-bit
  address auto-increments with R#14 carry. Palette and indirect-register ports
  use their own two-stage / pointer protocols.
- **Scanline**: `ScanLine` decodes a local descriptor, maps the output line
  (progressive or woven interlace) to source coordinates, renders background and
  sprites, and commits status; the final line completes a frame.
- **Command**: an R#46 write starts/stops/replaces a command; `StepCommand`
  advances one natural unit; CPU transfers use R#44 (input) and S#7 (output)
  with TR handshakes; completion or STOP clears CE/TR and the command nibble.

## Boundaries

The library depends only on the C standard library. It contains no Virtual Drip
host/proxy, packet, RFB, serial, networking, threading, Python, or timing
dependency, and does not modify `backends/vDrip9928`. Host scaling, framebuffer
ownership, and integration are the caller's responsibility.
