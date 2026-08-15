# Pacamara Video Card Architecture

**Status:** working design notes — not a frozen specification  
**Project:** pBITz / Coffee Series — Pacamara  
**Working name:** Pacamara video card (final product name TBD)  
**Current direction:** fully custom FPGA-based, RX660-assisted, three-layer indexed framebuffer video subsystem

This document captures the framebuffer architecture that was originally explored for the Afternoon Latte VDG and is now being moved to the Pacamara family.

The design was becoming too custom for the Afternoon Latte card: multiple framebuffer controllers, independent scrolling, page steering, a programmable compositor, and custom raster timing were pushing the design away from the original HD6445 tile-engine concept. That complexity is a better fit for Pacamara, where the goal is to build the video subsystem from currently available parts rather than depend on obsolete/eBay-only video silicon.

For Afternoon Latte, the design direction returns to the original **HD6445 + tilemap + pattern SRAM + serializer + ADV478** architecture, without the later goals of smooth scrolling, hardware sprites, multiple framebuffer layers, or a general framebuffer mode. A true framebuffer card for the pBITz machines remains the separate Hitachi ACRTC-based design.

The Pacamara card instead keeps the fully custom framebuffer ideas and implements the timing/address/compositor logic in an FPGA rather than a collection of ATF1504/ATF1508 CPLDs and discrete mux/comparator packages.

---

## 1. Architectural intent

The Pacamara video card uses one fundamental display model:

- **three independent 8-bit indexed framebuffer layers**;
- an **RX660 graphics coprocessor** as the software-defined renderer;
- an **FPGA** as the deterministic real-time video engine;
- external asynchronous SRAM for framebuffer storage;
- an indexed RAMDAC/output stage;
- no vintage CRTC or graphics controller in the scanout path.

Higher-level graphics concepts remain software abstractions:

- tiles are RX660 blits from a tile atlas;
- sprites/objects are RX660-rendered pixels on an overlay plane;
- fonts/text are RX660 rendering operations;
- scrolling is implemented by per-layer framebuffer start addresses;
- transparency and priority are implemented in the FPGA compositor;
- page flipping is requested by the RX660 and committed by the FPGA at a safe frame boundary.

The design bias is:

> **simple deterministic FPGA scanout + a capable software-defined renderer, using current-production components wherever practical.**

---

## 2. Design lineage

The architecture evolved through the following stages:

```text
1. Afternoon Latte HD6445 tile engine
      tile map + pattern RAM + serializer + ADV478

2. Framebuffer exploration
      RX660 becomes renderer; HD6445 becomes mostly timing

3. Three framebuffer layers
      background / objects / overlay without a sprite evaluator

4. HD6445 used CGA-style as framebuffer address source
      exposed the 14-bit MA ceiling, character-clock limits,
      RA-banked storage, and single shared MA stream

5. Custom CPLD address generators
      linear framebuffer addressing and true per-layer scrolling

6. One CPLD per layer + dedicated compositor logic
      technically workable, but package count and bus steering grew rapidly

7. Pacamara FPGA video engine
      preserve the custom framebuffer architecture,
      collapse timing/address/compositor logic into one FPGA,
      and stop constraining the design around small 5 V CPLDs
```

The FPGA version is therefore not a clean-sheet concept. It is the natural consolidation of the custom logic that was already emerging in the CPLD design.

---

## 3. High-level architecture

```text
 Host CPU
    |
    | graphics command / data transport
    v
+---------------------+
| Host-interface glue |
| small hardware FIFO |
| hardware backpressure
+----------+----------+
           |
           v
+--------------------------------+
| RX660                          |
|                                |
| FIFO ingress service           |
| SRAM command ring              |
| parser                         |
| renderer / blitter             |
| tile / sprite / font APIs      |
| decompression                  |
| palette control                |
| page-flip scheduler            |
+---------------+----------------+
                |
                | writes inactive framebuffer banks
                | programs FPGA video registers
                v

                       scanout side

                   +------------------------+
                   | FPGA                   |
                   |                        |
                   | raster timing          |
                   | L0 address generator   |
                   | L1 address generator   |
                   | L2 address generator   |
                   | page/bank control      |
                   | pixel capture pipeline |
                   | transparency compare   |
                   | priority compositor    |
                   | output timing          |
                   +-----------+------------+
                               |
          +--------------------+--------------------+
          |                    |                    |
      L0 FRONT SRAM        L1 FRONT SRAM        L2 FRONT SRAM
          |                    |                    |
       8-bit pixel           8-bit pixel           8-bit pixel
          +--------------------+--------------------+
                               |
                       FPGA compositor
                               |
                         final 8-bit index
                               |
                            RAMDAC
                               |
                              RGB
```

The three framebuffer memories remain physically independent scanout domains so all three layers can fetch simultaneously.

---

## 4. RX660 rendering model

The RX660 is the graphics coprocessor and is not in the real-time pixel scanout path.

The host submits commands through a small hardware FIFO. The RX660 drains that FIFO quickly into a larger internal SRAM command ring, then parses and renders work asynchronously.

The RX660 may implement operations such as:

- fill;
- copy/blit;
- masked blit;
- sprite/object drawing;
- tile drawing from an atlas;
- font/text rendering;
- image/RLE decompression;
- dirty-region updates;
- palette operations;
- per-layer scroll/start-address programming;
- page-flip requests.

The host can therefore see a retro-friendly graphics API while the physical scanout hardware sees only ordinary framebuffer pixels.

---

## 5. Three framebuffer layers

The current model contains three complete 8-bit indexed planes.

Natural software roles are:

```text
Layer 0   background / world
Layer 1   objects / gameplay foreground
Layer 2   HUD / UI / pointer / top overlay
```

These roles are conventions only. All three planes are general-purpose.

Each framebuffer byte is an 8-bit palette index.

The FPGA independently generates a linear framebuffer address for each layer, so each layer can have its own start address and therefore its own scrolling/parallax behavior.

---

## 6. Candidate framebuffer geometry

A useful concrete geometry inherited from the CPLD exploration is:

```text
visible viewport         800 x 600
physical framebuffer     832 x 630 x 8bpp
physical pitch           832 bytes
page size                832 x 630 = 524,160 bytes
```

This geometry provides:

- 32 pixels of horizontal scroll margin;
- 30 lines of vertical scroll margin;
- one byte = one pixel;
- simple linear addressing;
- a page that is very close to 512 KiB.

The RX660 computes a layer start address as:

```text
START = scroll_y * 832 + scroll_x
```

The FPGA does not need a multiplier in the pixel path.

For a fixed 832-byte pitch:

```text
frame start:
    ADDR = START

each visible pixel:
    ADDR += 1

end of 800-pixel visible line:
    ADDR += 32
```

The geometry is still a candidate, not a frozen requirement. An FPGA implementation makes a programmable pitch much easier if later useful.

---

## 7. Per-layer scanout engine

Each layer has its own retained configuration and live scanout state.

Minimum conceptual state:

```text
START                 retained framebuffer start address
ADDR                  live pixel address
PAGE                  current front/back bank selection
FLIP_REQUEST          pending page swap
BLANK                 optional overlay-layer disable
```

At frame start:

```text
ADDR <= START
```

During active display, the layer address advances one byte per pixel. At the end of the visible line, the address advances by the hidden portion of the physical pitch.

Unlike the earlier CPLD design, an FPGA gives enough resources that a more conventional form is also practical:

```text
LINE_BASE
FETCH_ADDR
PITCH
SCROLL_X
SCROLL_Y
```

The exact internal representation should be chosen for clean timing rather than macrocell economy.

---

## 8. Raster timing

The FPGA replaces the HD6445/6845-family CRTC entirely.

It directly generates:

- horizontal pixel count;
- vertical scanline count;
- HSYNC;
- VSYNC;
- active-display qualification;
- framebuffer line/frame reload strobes;
- pixel-pipeline timing.

Candidate modes remain:

```text
800 x 600 @ 60-class refresh    40.000 MHz nominal pixel clock
800 x 600 @ 56.25 Hz            36.000 MHz pixel clock
```

The 36 MHz VESA 56.25 Hz timing remains attractive for early bring-up because it gives the external SRAM path a 27.78 ns pixel period instead of 25 ns at 40 MHz.

The FPGA should make timing profiles programmable rather than encoding a 6845-style character/raster abstraction.

---

## 9. Framebuffer SRAM path

The current scanout concept remains one byte per pixel using fast asynchronous SRAM.

Per layer:

```text
FPGA address -> FRONT SRAM -> D[7:0] -> FPGA input register
```

The real timing constraint is:

```text
FPGA registered-address clock-to-output
+ PCB/address propagation
+ SRAM tAA
+ PCB/data propagation
+ FPGA input setup
< pixel period / chosen pipeline phase
```

A roughly 10 ns SRAM is the current conceptual target.

The FPGA implementation makes it easy to pipeline the input capture and compositor without requiring discrete '574 registers.

---

## 10. Pixel compositor

The compositor is now an FPGA function rather than a collection of discrete comparators, muxes, and registers.

The design retains the useful feature discovered during the discrete implementation: a **programmable transparent palette index**.

Conceptually:

```text
TRANSPARENT_KEY[7:0]     RX660-programmed register

opaque1 = (L1_PIXEL != TRANSPARENT_KEY) && !BLANK1
opaque2 = (L2_PIXEL != TRANSPARENT_KEY) && !BLANK2

if opaque2:
    OUT = L2_PIXEL
else if opaque1:
    OUT = L1_PIXEL
else:
    OUT = L0_PIXEL
```

Layer 0 is the default opaque background plane.

The FPGA can register all three incoming pixels on one common pixel clock, perform equality/priority selection in the next pipeline stage, and register the final 8-bit palette index before the RAMDAC.

A typical pipeline is:

```text
cycle N:
    capture L0/L1/L2 SRAM pixels

cycle N+1:
    compare transparency + priority select
    register final index

cycle N+2:
    RAMDAC consumes registered pixel
```

Pipeline latency is harmless as long as HS/VS/DE are delayed by the corresponding number of clocks.

Possible future compositor features, if they remain cheap in the selected FPGA, include:

- independent transparency keys per overlay layer;
- programmable layer order;
- forced layer/debug selection;
- global layer blanking;
- simple logical composition modes.

These are optional extensions, not baseline requirements.

---

## 11. Double buffering and SRAM ownership

Each layer is intended to be double-buffered with separate physical framebuffer SRAM banks:

```text
Layer 0A / Layer 0B
Layer 1A / Layer 1B
Layer 2A / Layer 2B
```

During a normal frame:

```text
FRONT bank   owned by FPGA scanout
BACK bank    owned by RX660 renderer
```

At a safe vertical-blank boundary, a requested layer flip exchanges the roles.

The earlier CPLD design exposed an important physical issue: separate SRAM packages eliminate time arbitration, but they do not eliminate **ownership steering**. Every physical bank must be connectable either to the scanout address/control path or to the RX660 address/data/control path.

In the FPGA version, the exact implementation remains open. Candidate approaches include:

- FPGA-controlled external bus switches/multiplexers;
- FPGA I/O directly implementing more of the ownership crossbar if the selected part has sufficient pins and voltage compatibility;
- revisiting the SRAM organization so front/back ownership requires less external steering;
- using a different current-production memory technology if it materially simplifies the interface.

The electrical invariant remains non-negotiable:

> visual corruption may be tolerated during deliberate experiments; two active drivers must never be allowed to fight on a bus.

---

## 12. Page flipping

The RX660 owns rendering completion; the FPGA owns safe display timing.

Per layer:

```text
RX660 finishes BACK page
        |
        v
sets FLIP_REQUEST
        |
        v
next safe frame boundary:
    PAGE ^= 1
    clear FLIP_REQUEST
```

Page flips are naturally per-layer.

If a layer misses its rendering deadline, it simply repeats the previous front page.

---

## 13. Host command ingress

The command-ingress concept from the Afternoon Latte exploration is retained as a useful starting point:

```text
Host CPU -> small hardware FIFO -> RX660 internal SRAM ring -> renderer
```

Current conceptual transport:

- 16-byte hardware FIFO;
- fixed 8-byte transport records;
- hard host backpressure when full;
- RX660 drains the FIFO quickly and performs expensive work later from its internal queue.

The protocol should not depend on the FIFO being exactly 16 bytes deep.

Exact command encoding remains open.

---

## 14. FPGA partition

The CPLD version had evolved toward four programmable devices:

```text
L0 CPLD       framebuffer address + raster timing
L1 CPLD       framebuffer address
L2 CPLD       framebuffer address
COMP CPLD     pixel registration + transparency + priority
```

Pacamara collapses these into one FPGA:

```text
+-----------------------------------------------------+
| FPGA                                                |
|                                                     |
| raster generator                                    |
|                                                     |
| L0 scanout engine   L1 scanout engine   L2 scanout engine
|       |                   |                   |      |
|       +-------------------+-------------------+      |
|                           |                          |
|                   pixel compositor                  |
|                           |                          |
|                   registered output                 |
|                                                     |
| RX660 register interface                            |
| page-flip state                                     |
| bank-steering control                               |
+-----------------------------------------------------+
```

This is the key architectural migration: the design no longer needs to optimize every feature around ATF1504/1508 macrocell and GPIO limits.

---

## 15. Why this belongs on Pacamara

This design is intentionally more custom than the Afternoon Latte card now needs to be.

For Pacamara it has several advantages:

- it does not depend on an obsolete CRTC or graphics controller;
- it can be built around currently available FPGA, SRAM, RAMDAC/output, and MCU parts;
- the raster engine is fully under project control;
- framebuffer geometry and timing are no longer constrained by a vintage CRTC;
- independent layers and scrolling are native rather than retrofitted;
- the compositor becomes trivial FPGA logic instead of a large discrete package cluster;
- future video modes can evolve in HDL without changing the conceptual host API.

The FPGA is being used as a deterministic video datapath, not as the machine's CPU or as an opaque SoC replacement for the whole computer.

---

## 16. Open questions

The design is intentionally not frozen. Major items still to resolve are:

1. **FPGA family and exact part.** Current-production availability, package, I/O count, voltage rails, toolchain, and long-term hobby sourcing matter more than raw logic capacity.
2. **Framebuffer memory technology.** Re-evaluate 5 V asynchronous SRAM versus newer memory now that level translation/power rails may already exist for the FPGA.
3. **A/B bank steering.** Decide whether external bus switches are still the cleanest solution or whether the FPGA/memory organization can remove most of the crossbar.
4. **Exact framebuffer geometry.** 832x630 around an 800x600 viewport remains a useful candidate, not a requirement.
5. **Pixel timing.** 36 MHz/56.25 Hz versus 40 MHz/60-class operation for the first implementation.
6. **RAMDAC/output device.** ADV478 is part of the inherited architecture; for Pacamara, a current-production output solution should be considered if the no-obsolete-parts objective is applied strictly.
7. **RX660 role.** Retain the MCU as the graphics renderer versus moving selected blitter primitives into FPGA hardware should be decided deliberately; the present architecture keeps the RX660 renderer.
8. **Host interface.** Exact bus, FIFO, register map, and bulk-transfer protocol depend on the final Pacamara CPU/backplane design.
9. **Transparency model.** One global programmable key is the current baseline; separate keys or more advanced alpha/blend behavior are optional.
10. **Output modes.** Initial VGA/RGB-style output versus direct digital output should be considered as part of the Pacamara platform rather than inherited automatically from Afternoon Latte.

---

## 17. Current architectural summary

The Pacamara video card currently means:

- one fully custom FPGA video engine;
- one RX660 graphics coprocessor/renderer;
- three independent 8-bit indexed framebuffer layers;
- independent per-layer start addresses and scrolling;
- double-buffered framebuffer memory;
- hardware page flips at frame boundaries;
- programmable transparency-key comparison;
- fixed layer priority L2 > L1 > L0 as the baseline;
- all three pixels captured and composited inside the FPGA;
- custom FPGA raster timing rather than HD6445/6845 timing;
- no tile, sprite, or character-cell semantics in scanout hardware;
- tiles/sprites/fonts remain RX660 software/rendering abstractions;
- current-production components are preferred, with obsolete/eBay-only video ICs deliberately avoided;
- final card name remains open.

The core idea is preserved from the Afternoon Latte exploration, but the implementation boundary has changed:

> **what had become a handful of cooperating CPLDs plus discrete compositor logic becomes one deliberately scoped FPGA video engine.**
