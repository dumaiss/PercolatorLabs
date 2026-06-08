# vDrip9928 — VDP Emulation Backend

vDrip9928 is the TMS9918-family video display processor (VDP) emulation backend
for the **Virtual Drip** console (the modern-host renderer for the Zephyr-80 /
Virtual Drip system). The Z80 CP/M host streams VDP register state and VRAM over
the Virtual Drip serial protocol, and this library renders the frame —
scanline by scanline — on the host side.

It is consumed by the `virtual-vdp` executable via the convenience header
[`vdrip_vdp.h`](src/vdrip_vdp.h) and the `vdrip_vdp` / `vdrip_vdp_util`
libraries.

## Attribution

vDrip9928 is a **permanent, independent fork** of
[**vrEmuTms9918**](https://github.com/visrealm/vrEmuTms9918) by
**Troy Schrapel** (Copyright © 2021 Troy Schrapel), used under the MIT License.

This fork is intentionally **not kept in sync with upstream**. Its public API has
been renamed into its own `vDrip9928` / `VDrip9928` symbol namespace (with
correspondingly renamed headers) so that it can coexist in the same executable
as the unmodified upstream `vrEmuTms9918` library without symbol collisions. The
original MIT copyright and license notices are retained in every source file and
in [`LICENSE`](LICENSE), as the license requires.

## How this fork diverges from upstream

* **TEXT 2 mode (80-column, 480×192)** — a new display mode (`TMS_MODE_TEXT_2`)
  with a dedicated scanline renderer (`vDrip9928Text2ScanLine`) and initialiser
  (`vDrip9928InitialiseText2`). This is the headline feature the Virtual Drip
  console relies on.
* **Full-width background pre-fill** — the scanline buffer is pre-filled with the
  background color so modes that use only part of the 512-pixel width leave clean
  padding.
* **`vDrip9928` / `VDrip9928` namespace** — every public function, the exported
  palette, and the public types carry the fork's namespace; the public headers
  are `vDrip9928.h` and `vDrip9928Util.h`.

## Supported modes

* Graphics I (including sprites)
* Graphics II (including sprites)
* Multicolor (including sprites)
* Text
* **Text 2 (80-column)** *(fork addition)*

Other emulated features inherited from upstream: 5th-sprite handling, sprite
collisions, VSYNC interrupt, and individual scanline rendering.

## Usage

```c
#include "vdrip_vdp.h"   /* pulls in vDrip9928.h + vDrip9928Util.h */

VDrip9928 *vdp = vDrip9928New();
/* feed register/VRAM writes ... */
uint8_t scanline[TMS9918_PIXELS_X];
vDrip9928ScanLine(vdp, y, scanline);
/* ... */
vDrip9928Destroy(vdp);
```

Link against `vdrip_vdp` (core) and, if you use the helper/initialiser
functions, `vdrip_vdp_util`.

## Building

This backend is built automatically as part of the Virtual Drip build — the
top-level `CMakeLists.txt` pulls it in with `add_subdirectory(backends/vDrip9928)`
and collects the resulting shared libraries into the build's `externals/`
directory, where the `virtual-vdp` executable locates them at link and run time.

To build it stand-alone:

```sh
cmake -S backends/vDrip9928 -B backends/vDrip9928/build
cmake --build backends/vDrip9928/build
```

## License

MIT — see [LICENSE](LICENSE). Original work Copyright © 2021 Troy Schrapel
(vrEmuTms9918); fork modifications for the Virtual Drip console.
