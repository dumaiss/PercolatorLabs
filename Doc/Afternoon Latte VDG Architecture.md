# Afternoon Latte VDG Architecture

**Status:** working design notes — not a frozen specification  
**Project:** pBITz / Coffee Series — Afternoon Latte VDG  
**Current direction:** RX660-rendered, three-layer indexed framebuffer with one local CPLD controller per layer

This document is the current architectural anchor for the Afternoon Latte VDG. It records the design as it exists after moving away from the earlier HD6445/tile-engine architecture. Open questions are left explicit rather than papered over.

---

## 1. Design direction

The VDG began as an HD6445-based tile engine:

```text
HD6445 -> tile map -> pattern RAM -> serializer -> indexed RAMDAC
```

That architecture was useful for exploring tiles, palette banking, sprites, and smooth scrolling, but progressively accumulated special cases when a framebuffer mode was added.

The current direction is to support **one fundamental scanout model: linear 8-bit indexed framebuffers**.

Higher-level concepts remain available, but are implemented by the RX660 rather than by separate scanout engines:

- tiles are RX660 blits from a tile atlas;
- sprites/objects are RX660-rendered pixels on an overlay layer;
- fonts are RX660 rendering operations;
- scrolling is a framebuffer start-address change;
- page flipping is a per-layer hardware operation committed at vertical blank.

The resulting split is intentional:

- the **host CPU** submits graphics commands and data;
- the **RX660** queues and renders those commands;
- the **three layer controllers** generate deterministic framebuffer addresses;
- the **pixel path** reads SRAM, registers pixels, composites layers, and drives the RAMDAC;
- the RX660 never participates in pixel-by-pixel scanout.

---

## 2. Design lineage

The architecture passed through several useful intermediate forms:

```text
1. HD6445 tile engine
      tile map + pattern RAM + serializer

2. Single framebuffer
      RX660 becomes renderer; CRTC increasingly becomes timing only

3. Three framebuffer layers
      background / objects / overlay without dedicated sprite hardware

4. HD6445 used CGA-style as framebuffer address source
      exposed the 14-bit MA ceiling, character-clock limits,
      RA-banked memory layout, and one-MA-stream limitation

5. CPLD-generated framebuffer addresses
      linear memory, true per-layer scrolling, no CRTC address ceiling

6. One CPLD per layer
      forced by the physical reality of three independent address buses;
      Layer 0 also becomes the one raster/sync timing master
```

The present architecture has **three programmable devices total**, not four:

```text
Layer 0 CPLD = framebuffer controller + timing master
Layer 1 CPLD = framebuffer controller, timing slave
Layer 2 CPLD = framebuffer controller, timing slave
```

The HD6445 is removed from this design.

---

## 3. High-level architecture

```text
 Host CPU
    |
    | fixed-size command / data records
    v
+---------------------+
| Host-interface glue |
| 16-byte FIFO        |
| hardware backpressure
+----------+----------+
           |
           v
+--------------------------------+
| RX660                          |
|                                |
| FIFO ingress service           |
| SRAM command ring              |
| command parser                 |
| renderer / blitter             |
| tile / sprite / font APIs      |
| palette management             |
| page-flip scheduler            |
+---------------+----------------+
                |
                | writes BACK framebuffers
                | programs layer registers
                v

                       scanout side

                         PIXEL CLOCK
                              |
                              v
                    +--------------------+
                    | Layer 0 CPLD       |
                    | TIMING MASTER      |
                    | address generator  |
                    | HS / VS / DE       |
                    +---------+----------+
                              |
                    synchronized timing
                    /                     \
                   v                       v
          +----------------+      +----------------+
          | Layer 1 CPLD   |      | Layer 2 CPLD   |
          | timing slave   |      | timing slave   |
          | address gen    |      | address gen    |
          +--------+-------+      +--------+-------+
                   |                       |

       L0 SRAM A/B          L1 SRAM A/B          L2 SRAM A/B
           |                    |                    |
       pixel reg             pixel reg             pixel reg
           |                    |                    |
           +--------------------+--------------------+
                                |
                     transparent-priority
                          compositor
                                |
                         final pixel reg
                                |
                              ADV478
                                |
                               RGB
```

The three framebuffer layers are physically independent memory islands. They fetch simultaneously and never share a scanout data bus.

---

## 4. Host CPU interface

### 4.1 Hardware FIFO

The host-to-VDG boundary uses a small hardware FIFO to absorb timing differences between the retro host bus and the RX660.

Current proposal:

- **16-byte FIFO**;
- host-side hardware backpressure when full;
- RX660 service indication when data is available;
- preferably an indication that at least one complete transport record is present.

The FIFO is **not** the command queue. It is only a timing-elasticity buffer.

The real queue is in RX660 internal SRAM:

```text
Host -> 16-byte FIFO -> RX660 SRAM ring -> command consumer -> renderer
```

### 4.2 Fixed 8-byte records

The current transport proposal uses fixed **8-byte records**.

A 16-byte FIFO therefore holds exactly two complete records.

Provisional transport shape:

```text
byte 0      opcode / record type
byte 1      flags / layer / subtype
byte 2..7   parameters or payload
```

Small operations can fit in one record. Bulk uploads can be represented as a sequence of fixed-size records.

The exact command encoding is not yet frozen.

### 4.3 Backpressure contract

When the hardware FIFO is full, the host write cycle is stretched using the host's WAIT/READY mechanism until the RX660 drains enough data.

Therefore FIFO depth affects **how often the host stalls**, not correctness.

The protocol must not depend on the FIFO being exactly 16 bytes deep so that a later implementation can use a larger FIFO without changing software.

---

## 5. RX660 scheduling model

The RX660 is single-core. The architecture deliberately separates **ingress** from **execution** so that this is not a problem.

The highest-priority ingress task does as little work as possible:

```text
while FIFO has data:
    copy bytes / complete records into SRAM ring
return
```

Parsing and rendering happen later from the SRAM queue.

Conceptually:

```text
[GULP incoming records]
        |
[consume / render queued work]
        |
[GULP incoming records]
        |
[continue rendering]
```

Long rendering operations must be interruptible or chunked so FIFO servicing has bounded latency.

The architecture separates three rates:

1. host production rate;
2. FIFO-to-RX660 ingestion rate;
3. RX660 rendering/execution rate.

The RX660 only needs to **ingest** fast enough to keep up with the host. It does not need to **render** at host submission rate.

---

## 6. Rendering model

The RX660 is the sole graphics renderer.

It writes inactive framebuffer banks and may implement:

- fill;
- copy/blit;
- masked blit;
- sprite/object drawing;
- tile drawing from an atlas;
- text/font rendering;
- image/RLE decoding;
- dirty-region updates;
- palette operations;
- per-layer start-address changes;
- page-flip requests.

The host may still use a retro-style API such as `DRAW_TILE` or `SPRITE_DRAW`, but those names describe RX660 operations, not special hardware scanout formats.

---

## 7. Three display layers

The card contains three complete 8-bit indexed framebuffer layers.

Natural software conventions are:

```text
Layer 0   background / world
Layer 1   objects / sprite-like content
Layer 2   HUD / UI / pointer / top overlay
```

These are conventions only. Applications may use all three as general-purpose independent planes.

Each framebuffer byte is directly an 8-bit ADV478 palette index.

For Layers 1 and 2, index `0x00` is the fixed transparent key. Layer 0 is always opaque; index zero is a normal background color there.

---

## 8. Three-CPLD partition

### 8.1 Layer 0 / Timing Master

Layer 0 uses the larger programmable device because it has two responsibilities:

1. generate Layer 0 framebuffer addresses;
2. generate the common raster timing.

Current part direction:

```text
Layer 0 / Timing Master    ATF1508AS-AU100 class
Layer 1                    ATF1504AS-AU100 class
Layer 2                    ATF1504AS-AU100 class
```

Exact fitter results remain a validation item; the split reflects the expected register/macrocell pressure rather than a frozen BOM.

The master generates:

- HS;
- VS;
- DE or equivalent active-display qualification;
- frame-start and line-start timing events as required;
- any common pixel-capture phase/strobe used by all three layer output registers.

### 8.2 Layer 1 / Layer 2 timing slaves

The blind layers do **not** free-run their own complete raster generators.

They use the common pixel clock and timing events from Layer 0 so all three layers present the same raster coordinate on the same pixel.

The implementation must explicitly account for master-CPLD clock-to-output delay. A timing signal generated on a pixel-clock edge cannot be assumed to be usable as a same-edge synchronous input in another CPLD.

Therefore line/frame reload strobes must be generated early enough, or on an appropriate phase/edge, to meet setup at the layer controllers' active clock edge.

This is a bench-validation item, not something to leave implicit.

---

## 9. Per-layer framebuffer controller

Each layer CPLD is a small linear framebuffer address generator.

A critical correction from earlier notes is that the live address counter **cannot also be the retained start-address register**.

Each layer therefore needs at least:

```text
START[18:0]        retained, RX660-programmed
ADDR[18:0]         live scanout counter
PAGE               current front/back ownership
FLIP_REQUEST       pending swap request
BLANK              L1/L2 only
```

At frame start:

```text
ADDR <= START
```

During the active raster, `ADDR` advances through the framebuffer.

The RX660 computes the start address. The CPLD does not perform coordinate multiplication.

---

## 10. Candidate framebuffer geometry

A concrete candidate geometry for an 800x600 visible mode is:

```text
physical framebuffer    832 x 630 x 8bpp
visible viewport         800 x 600
physical pitch           832 bytes
page size                832 x 630 = 524,160 bytes
```

This fits almost exactly in a 512 KiB byte-wide SRAM page.

It also provides:

- 32 pixels of horizontal margin;
- 30 lines of vertical margin;
- direct one-pixel scrolling without requiring a general programmable pitch unit.

The visible viewport is only a window into the larger physical surface.

### 10.1 Address sequencing

For the fixed 832-byte physical pitch:

```text
frame start:
    ADDR = START

each visible pixel:
    ADDR += 1

end of 800-pixel visible line:
    ADDR += 32
```

The RX660 calculates:

```text
START = scroll_y * 832 + scroll_x
```

No multiplier exists in the CPLD.

No coarse/fine horizontal scroll mechanism exists either: one byte is one pixel, so a one-byte change in START is a one-pixel horizontal move.

A vertical move by one source line changes START by 832 bytes.

### 10.2 Example

With viewport origin `(2,1)`:

```text
START = 1 * 832 + 2 = 834
```

The first displayed line reads 800 bytes starting at address 834, then the controller skips 32 bytes before the next visible line.

This avoids the line-wrap bug that occurs if horizontal scrolling is attempted inside a tightly packed 800-byte pitch.

---

## 11. Framebuffer SRAM organization

### 11.1 One byte per pixel

Current scanout direction is:

- **8-bit asynchronous SRAM**;
- one SRAM location = one pixel;
- one SRAM read per pixel;
- no wide-word unpacker;
- no tile serializer;
- no pixel-phase mux.

At 800x600@60 the nominal pixel clock is 40 MHz, giving a 25 ns pixel period.

A 10 ns SRAM is a plausible candidate, but the timing condition is not merely `25 ns - 10 ns`.

The actual path that must be proven is approximately:

```text
CPLD counter clock-to-output
+ PCB address propagation
+ SRAM address-access time
+ PCB data propagation
+ output-register setup
< one pixel period / chosen capture phase
```

The 40 MHz direct-fetch design is therefore **plausible and intentionally simple**, but must be validated against the exact CPLD speed grade, SRAM part, clock phase, and register family.

### 11.2 Pixel data stays out of the CPLDs

The SRAM data bus does not enter the layer CPLD.

Per layer:

```text
CPLD A[18:0] -> FRONT SRAM
                    |
                    | D[7:0]
                    v
              output register
                    |
                    v
              LAYER_PIXEL[7:0]
```

This preserves CPLD I/O for address generation and control.

All three layer pixel registers should use a **common capture clock/phase**, rather than independently generated capture clocks from each layer CPLD, so their outputs are aligned before composition.

---

## 12. Double buffering and SRAM ownership

Each layer is double-buffered using two physically separate framebuffer SRAM packages:

```text
Layer 0A / Layer 0B
Layer 1A / Layer 1B
Layer 2A / Layer 2B
```

During a normal frame:

- scanout owns the FRONT SRAM;
- the RX660 owns the BACK SRAM.

At vertical blank the roles may swap.

This removes **time arbitration** between scanout and rendering during normal operation, but it does **not** eliminate the electrical ownership problem.

Each physical SRAM must be capable of being connected to either:

- the layer CPLD/video address source when it is FRONT; or
- the RX660 address/data/control bus when it is BACK.

Therefore each layer still requires explicit **bank steering / ownership multiplexing**.

Conceptually:

```text
                      VIDEO ADDRESS
                           |
RX660 ADDRESS ------------+---- ownership steering ---- SRAM A
                           |
                           +---- ownership steering ---- SRAM B

PAGE=A:
    SRAM A <- VIDEO address/control, data -> pixel register
    SRAM B <- RX660 address/data/control

PAGE=B:
    SRAM B <- VIDEO address/control, data -> pixel register
    SRAM A <- RX660 address/data/control
```

The exact steering circuit is a major open hardware item and must be drawn explicitly before the memory architecture is considered frozen.

The separate packages remove per-cycle video/MCU arbitration; they do not magically remove address/data/control muxing.

### 12.1 SRAM control pins

`/CE`, `/OE`, and `/WE` cannot simply be regarded as permanently tied for a physical SRAM package independent of PAGE state, because that same package alternates between video-read and RX660-read/write roles.

The steering logic must ensure that only the current owner drives each SRAM and that no bus fight is possible during or around a page swap.

---

## 13. Page flipping

The RX660 decides when a rendered back page is complete. Hardware decides when the ownership swap is electrically safe.

Per layer:

```text
RX660 finishes BACK page
        |
        v
sets FLIP_REQUEST
        |
        v
next vertical-blank / frame boundary:
    if FLIP_REQUEST:
        PAGE ^= 1
        clear request
```

Page flipping is **per layer** in the current direction.

If a layer misses its rendering deadline, the RX660 simply does not request that layer's flip; the previous page remains visible for another frame.

The exact PAGE/FLIP_REQUEST register placement is still an implementation choice, but the semantics are part of the architecture.

---

## 14. RX660-to-CPLD register interface

The RX660 programs each layer controller through a small write-only register port, conceptually similar to a classic peripheral register interface.

Candidate shared signals:

```text
D0..D7       register data
RS / register index mechanism
/WR          write strobe
```

with one chip select per layer:

```text
/CS_L0
/CS_L1
/CS_L2
```

The current architecture does not require CPLD register readback. The RX660 owns the software-visible state and initializes all controller state after reset.

The exact register map is not yet frozen.

At minimum it must support programming retained `START`, flip request, and overlay blank state; the timing master additionally needs timing configuration only if runtime-programmable video timing is eventually desired.

---

## 15. Scrolling and parallax

Because framebuffer storage is linear and byte-addressed, smooth scrolling is fundamentally a start-address operation.

For the candidate 832-byte physical pitch:

```text
scroll right one source pixel:   START += 1
scroll down one source line:     START += 832
```

The RX660 computes the desired START value and writes it to the appropriate layer controller.

Each layer owns a separate START register, so independent scrolling is natural:

```text
L0 START -> distant background
L1 START -> gameplay plane
L2 START -> stationary HUD or independent overlay
```

This gives true per-layer parallax without a dedicated sprite or tile-scroll engine.

The START value should normally be committed at a controlled frame boundary unless an intentional raster effect is desired.

---

## 16. Sprite/object model

There is no dedicated sprite scanout unit in the present design.

The RX660 implements objects by drawing them into an overlay framebuffer, normally Layer 1.

Example abstraction:

```text
SPRITE_DRAW(layer=1, object, x, y)
```

The RX660 performs clipping, transparency/masking, and pixel writes into the inactive page. The finished page is then flipped into view.

Advantages over software sprites composited into a tile map include:

- no background restoration in scanout hardware;
- no sprite-per-line limit;
- no dynamic tile-slot management;
- no tile palette-bank conflict;
- arbitrary pixel positioning;
- simple overlap handled by the RX660 renderer;
- the scanout hardware never needs to know what a sprite is.

---

## 17. Pixel compositor

The three registered layer pixels converge only at the compositor.

Priority is:

```text
if L2_PIXEL != 0 and !BLANK_L2:
    OUT = L2_PIXEL
elif L1_PIXEL != 0 and !BLANK_L1:
    OUT = L1_PIXEL
else:
    OUT = L0_PIXEL
```

Layer 0 is always opaque.

The current preferred implementation keeps the 24 incoming pixel bits **out of the CPLDs** and uses discrete fast logic near the ADV478.

Conceptually:

```text
L0 ----\
        MUX A ----\
L1 ----/           MUX B ---> final register ---> ADV478
                  /
L2 ---------------
```

Two overlay opaque detectors produce the mux-select conditions.

An 8-bit mux stage requires enough actual packages for an 8-bit datapath; generic references such as "74x157-class" describe the function, not necessarily a one-package implementation.

The complete path must be timing-budgeted with the exact logic family:

```text
layer output register
-> opaque detect
-> mux stage A
-> mux stage B
-> final-register setup
```

The final output is registered before the RAMDAC.

---

## 18. Raster timing

Layer 0 contains the only full raster generator.

Candidate primary mode:

```text
800 x 600 @ 60 Hz
40.000 MHz pixel clock
```

The master CPLD maintains horizontal and vertical timing counters and derives HS, VS, active-display qualification, and synchronization strobes for the layer controllers.

This is no longer constrained by the HD6445's MA width or character-clock ceiling.

The architecture may support other timing profiles later, but the initial hardware should be designed around one thoroughly validated mode rather than adding multi-resolution complexity prematurely.

### 18.1 Common timing distribution

The blind layers must be locked to the master at the actual synchronous edge level.

Do not rely on a master-generated DE transition appearing soon enough to be consumed by the blind CPLDs on the same pixel-clock edge that created it.

The timing master should export explicit frame/line/active strobes on a phase that provides adequate setup to all three layer controllers.

Likewise, all three SRAM output registers should receive a common capture clock/phase.

This avoids deterministic one-pixel skew between layers.

---

## 19. Clocking

One pixel-clock source feeds all three layer controllers and the pixel pipeline.

For the 800x600 candidate:

```text
pixel clock = 40.000 MHz
pixel period = 25 ns
```

A higher-frequency source or phase-generation chain may still be useful during bring-up to place:

- address-counter transitions;
- SRAM capture edges;
- line/frame synchronization strobes;
- final RAMDAC registration

at experimentally favorable points inside the timing window.

The philosophy remains: keep the displayed pixel clock regular; use internal phases to satisfy propagation/setup timing.

---

## 20. Part partition

Current working part direction:

```text
L0 / Timing Master   ATF1508AS-AU100 class
L1 controller        ATF1504AS-AU100 class
L2 controller        ATF1504AS-AU100 class
```

The reason for the larger L0 part is macrocell/register pressure, not a fourth logical block.

Each blind controller must retain both START and live ADDR, so a realistic blind-layer macrocell estimate includes at least:

```text
19  START bits
19  ADDR bits
 1  PAGE
 1  FLIP_REQUEST
 1  BLANK (L1/L2)
 + register-interface/control terms
```

The master additionally needs the raster counters and timing state.

The fitter, not arithmetic estimates, decides final device fit.

---

## 21. Host-to-screen transaction

A typical draw operation follows this path:

```text
Host CPU
   |
   | 8-byte records
   v
hardware FIFO
   |
   v
RX660 ingress
   |
   v
RX660 SRAM command ring
   |
   v
renderer
   |
   | writes inactive framebuffer
   v
Layer N BACK SRAM
```

Simultaneously:

```text
Layer 0 FRONT SRAM -> registered L0 pixel --\
Layer 1 FRONT SRAM -> registered L1 pixel ----> compositor -> final reg -> ADV478
Layer 2 FRONT SRAM -> registered L2 pixel --/
```

At vertical blank, any requested per-layer bank swaps are committed.

The renderer and scanout are therefore decoupled at the framebuffer-page boundary.

---

## 22. Electrical design principles

The architecture is intentionally biased toward electrically obvious ownership.

- Three layers use three separate SRAM islands.
- Pixel data does not traverse the CPLDs.
- Video and RX660 normally operate on different physical SRAM packages.
- Address/data/control ownership of A/B banks must nevertheless be explicitly steered.
- Page ownership changes only at a controlled boundary.
- No two devices may drive the same SRAM or data bus simultaneously.
- Host FIFO overflow is prevented by hardware backpressure.
- Intentional visual glitches may be tolerated for experiments; electrical bus fights are never tolerated.

---

## 23. What this architecture removes from the older design

```text
REMOVED  HD6445 / MC6845-family CRTC
REMOVED  14-bit MA ceiling
REMOVED  character-clock ceiling
REMOVED  RA-banked CGA-style framebuffer storage
REMOVED  tilemap SRAM
REMOVED  pattern SRAM
REMOVED  ATTR/PIX tile palette split
REMOVED  tile serializer
REMOVED  coarse/fine tile-scroll mechanism
REMOVED  dedicated sprite-engine requirement
REMOVED  one shared MA stream for all layers

ADDED    one local framebuffer CPLD per layer
ADDED    L0 timing-master responsibility
ADDED    19-bit retained START + live ADDR per layer
ADDED    true independent per-layer pixel scroll
ADDED    explicit A/B SRAM ownership steering
ADDED    three direct pixel-rate SRAM read paths
```

The design remains retro in structure — counters, SRAM, discrete pixel muxing, indexed RAMDAC — but no longer keeps the vintage CRTC when the framebuffer architecture would require substantial logic to work around it.

---

## 24. Open questions / validation list

The following items are deliberately not frozen:

1. **A/B bank steering circuit.** This is now the highest-priority unresolved physical circuit: 19-bit address ownership, RX660 data path, `/CE`, `/OE`, `/WE`, and safe PAGE transition.
2. **Blind-layer fitter result.** Confirm START + ADDR + register decode + PAGE/FLIP/BLANK fits comfortably in ATF1504AS-AU100.
3. **Master fitter result.** Confirm raster counters + L0 framebuffer controller fit comfortably in ATF1508AS-AU100.
4. **Exact SRAM part.** 512Kx8, approximately 10 ns is the current conceptual target; exact stocked 5 V part and package remain to be selected.
5. **40 MHz timing closure.** Validate `CPLD tCO + SRAM tAA + routing + output-register setup` with exact parts and capture phase.
6. **Common capture clock implementation.** All three layer pixel registers should capture on one controlled phase.
7. **Master-to-slave timing strobes.** Establish line/frame reload timing with explicit setup margin; do not rely casually on same-edge DE fanout.
8. **Physical framebuffer geometry.** 832x630 is an attractive 512 KiB candidate for an 800x600 viewport, but should be frozen only after scroll-margin requirements are agreed.
9. **Register map.** Exact write-only RX660-to-CPLD register encoding.
10. **Host FIFO implementation.** Exact glue part and host WAIT/READY wiring.
11. **8-byte transport format.** Exact opcode/data-record semantics.
12. **Compositor logic family and timing.** Select actual fast 8-bit mux and opaque-detect parts and verify the two-stage path to the final register.
13. **ADV478 interface timing.** Verify final register edge and RAMDAC setup/hold against the chosen clock phase.
14. **Runtime timing programmability.** Initial preference is one fixed, validated 800x600 timing; multi-resolution operation can be reconsidered later.

---

## 25. Current architectural summary

The current Afternoon Latte VDG is:

- one RX660 graphics coprocessor;
- one small host-side FIFO with hardware backpressure;
- fixed 8-byte transport records as the current command-ingress proposal;
- a larger command ring in RX660 internal SRAM;
- three independent 8-bit indexed framebuffer layers;
- two physical SRAM banks per layer for front/back buffering;
- one CPLD local to each framebuffer layer;
- **three CPLDs total**;
- Layer 0's CPLD is also the sole raster/sync timing master;
- Layer 1 and Layer 2 are timing slaves;
- retained START and live ADDR are separate registers/counters per layer;
- candidate visible mode 800x600x8bpp at 60 Hz;
- candidate physical framebuffer geometry 832x630x8bpp per 512 KiB page;
- one byte/pixel SRAM fetch at pixel rate;
- pixel data bypasses the CPLDs and enters common-clocked output registers;
- sprites, tiles, text, and blits are RX660 firmware/rendering abstractions;
- independent per-layer scrolling comes from independent START values;
- page flips are RX660-requested and frame-boundary committed;
- Layer 1/2 use index zero as transparency;
- a discrete transparent-priority compositor selects L2, then L1, then L0;
- the composited index is registered before the ADV478;
- the biggest unresolved hardware block is the A/B SRAM ownership steering.

The guiding bias remains:

> **simple deterministic scanout hardware + a capable software-defined renderer, with each electrical responsibility kept local and explicit.**
