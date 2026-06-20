# vDrip9958 — Yamaha V9958 Emulation Backend

vDrip9958 is a standalone, deterministic C emulation of the **Yamaha V9958**
video display processor (VDP) for the Zephyr-80 / **Virtual Drip** system. It
emulates the software-visible V9958 register, status, palette, and 128 KiB VRAM
interfaces, renders native-size RGB scanlines for the documented display modes,
and executes the V9938/V9958 VRAM command set.

## What it is

- A portable C11 library (`vdrip9958`), shared or static.
- Deterministic and host-independent: no timing clock, threads, networking, or
  display dependencies. The caller drives scanline rendering and command
  stepping explicitly.
- Fixed **128 KiB** display VRAM.
- **Functional** (not cycle-accurate) timing: command progress and CE/TR are
  observable through explicit stepping rather than VDP cycles.

## What it is not

- Not a Virtual Drip host/proxy integration (that is a separate project).
- Not cycle-accurate, and it does not implement external video, color-bus,
  light-pen, mouse, or WAIT functions (see [docs/deviations.md](docs/deviations.md)).
- Not a golden-image-verified reference; correctness is covered by smoke tests
  plus manual review against the Yamaha manuals.

## Quick start

```sh
cmake -S backends/vDrip9958 -B backends/vDrip9958/build
cmake --build backends/vDrip9958/build
ctest --test-dir backends/vDrip9958/build --output-on-failure
```

Minimal usage:

```c
#include "vDrip9958.h"

VDrip9958* vdp = vDrip9958New();          /* allocate + reset */
uint32_t   line[VDRIP9958_MAX_WIDTH];

vDrip9958ScanLine(vdp, 0, line);          /* render one 0x00RRGGBB scanline */

VDrip9958DisplayInfo info = vDrip9958GetDisplayInfo(vdp);
/* info.width / info.height / info.mode ... */

vDrip9958Destroy(vdp);
```

## Documentation

See [docs/README.md](docs/README.md) for the full index. Highlights:

- [docs/api-reference.md](docs/api-reference.md) — public API contract.
- [docs/architecture.md](docs/architecture.md) — component design.
- [docs/register-support.md](docs/register-support.md),
  [docs/display-mode-support.md](docs/display-mode-support.md),
  [docs/command-support.md](docs/command-support.md) — support matrices.
- [docs/building.md](docs/building.md), [docs/testing.md](docs/testing.md).
- [docs/deviations.md](docs/deviations.md),
  [docs/verification.md](docs/verification.md).

The Yamaha technical manuals are retained under `docs/` as the authoritative
specifications.

## Attribution and license

MIT — see [LICENSE](LICENSE). vDrip9958 began from the Virtual Drip `vDrip9928`
backend, itself a permanent fork of
[**vrEmuTms9918**](https://github.com/visrealm/vrEmuTms9918) by **Troy Schrapel**
(Copyright © 2021 Troy Schrapel), used under the MIT License. The V9958 register
model, display modes, palette/color systems, command engine, and 128 KiB VRAM
behavior are new work. Original MIT notices are retained in the source and in
`LICENSE`.
