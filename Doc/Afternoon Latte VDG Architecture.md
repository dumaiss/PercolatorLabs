# Afternoon Latte VDG Architecture

**Status:** working design notes — not a frozen specification  
**Project:** pBITz / Coffee Series — Afternoon Latte VDG  
**Current direction:** RX660-rendered, three-layer indexed framebuffer with one local CPLD controller per layer

This document is the current architectural anchor for the Afternoon Latte VDG. It records the design after moving away from the earlier HD6445/tile-engine architecture. Open questions are left explicit rather than papered over.

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

```text
1. HD6445 tile engine
      tile map + pattern RAM + serializer

2. Single framebuffer
      RX660 becomes renderer; CRTC increasingly becomes timing only

3. Three framebuffer layers
      background / objects / overlay without dedicated sprite hardware

4. HD6445 used CGA-style as framebuffer address source
      exposed 14-bit MA ceiling, character-clock limits,
      RA-banked storage, and one-MA-stream limitation

5. CPLD-generated framebuffer addresses
      linear memory, true per-layer scrolling, no CRTC address ceiling

6. One CPLD per layer
      forced by three independent address buses;
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
                  programmable-key priority
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

The FIFO is **not** the command queue. The real queue is in RX660 internal SRAM:

```text
Host -> 16-byte FIFO -> RX660 SRAM ring -> command consumer -> renderer
```

### 4.2 Fixed 8-byte records

The current transport proposal uses fixed **8-byte records**. A 16-byte FIFO therefore holds exactly two complete records.

Provisional record shape:

```text
byte 0      opcode / record type
byte 1      flags / layer / subtype
byte 2..7   parameters or payload
```

Small operations can fit in one record. Bulk uploads can be represented as a sequence of fixed-size records. The exact command encoding is not yet frozen.

### 4.3 Backpressure contract

When the hardware FIFO is full, the host write cycle is stretched using the host's WAIT/READY mechanism until the RX660 drains enough data.

FIFO depth affects **how often the host stalls**, not correctness. The protocol must not depend on the FIFO being exactly 16 bytes deep.

---

## 5. RX660 scheduling and rendering model

The RX660 is single-core. The architecture deliberately separates **ingress** from **execution**.

The high-priority ingress service does as little work as possible:

```text
while FIFO has data:
    copy bytes / complete records into SRAM ring
return
```

Parsing and rendering happen later from the SRAM queue. Long rendering operations must be interruptible or chunked so FIFO servicing has bounded latency.

The RX660 is the sole graphics renderer and may implement:

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

The host may still use APIs such as `DRAW_TILE` or `SPRITE_DRAW`, but those names describe RX660 operations, not special scanout formats.

---

## 6. Three display layers

The card contains three complete 8-bit indexed framebuffer layers.

```text
Layer 0   background / world
Layer 1   objects / sprite-like content
Layer 2   HUD / UI / pointer / top overlay
```

These are software conventions only. Applications may use all three as general-purpose independent planes.

Each framebuffer byte is directly an 8-bit ADV478 palette index.

Layer 0 is always opaque. Layers 1 and 2 use **color-key transparency**: a pixel is transparent when its 8-bit palette index equals the programmable transparency key.

The current direction is one **shared 8-bit transparency key** for both overlay layers. A single RX660-programmed latch drives both overlay comparators. Separate keys per layer could be added later if a real use case appears.

---

## 7. Three-CPLD partition

### 7.1 Layer 0 / Timing Master

Layer 0 has two responsibilities:

1. generate Layer 0 framebuffer addresses;
2. generate the common raster timing.

Current part direction:

```text
Layer 0 / Timing Master    ATF1508AS-AU100 class
Layer 1                    ATF1504AS-AU100 class
Layer 2                    ATF1504AS-AU100 class
```

Exact fitter results remain a validation item.

The master generates:

- HS;
- VS;
- DE or equivalent active-display qualification;
- frame-start and line-start events;
- the common pixel-capture phase/strobe used by all three layer output registers.

### 7.2 Layer 1 / Layer 2 timing slaves

The blind layers do **not** free-run complete raster generators. They use the common pixel clock and timing events from Layer 0 so all three layers present the same raster coordinate on the same pixel.

Master-CPLD clock-to-output delay must be accounted for. A timing signal generated on a pixel-clock edge cannot be assumed usable as a same-edge synchronous input in another CPLD. Line/frame reload strobes must therefore be generated on a phase/edge that provides explicit setup margin.

---

## 8. Per-layer framebuffer controller

Each layer CPLD is a small linear framebuffer address generator.

The retained start address and the live scanout counter are separate state:

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

The RX660 computes START. The CPLD performs no coordinate multiplication.

---

## 9. Candidate framebuffer geometry

A concrete candidate geometry for an 800x600 visible mode is:

```text
physical framebuffer    832 x 630 x 8bpp
visible viewport         800 x 600
physical pitch           832 bytes
page size                832 x 630 = 524,160 bytes
512 KiB SRAM capacity    524,288 bytes
headroom                         128 bytes
```

This intentionally spends almost the entire 512 KiB page in exchange for simple address sequencing and a useful scroll border:

- 32 pixels of horizontal margin;
- 30 lines of vertical margin.

For the fixed 832-byte pitch:

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

A one-byte START change is a one-pixel horizontal scroll. A one-source-line change is 832 bytes.

At maximum viewport displacement `(32,30)`, the final visible pixel lands at byte 524,159, so the whole 800x600 window remains inside the 512 KiB page.

---

## 10. Framebuffer SRAM organization and timing

### 10.1 One byte per pixel

Current scanout direction is:

- **8-bit asynchronous SRAM**;
- one SRAM location = one pixel;
- one SRAM read per pixel;
- no wide-word unpacker;
- no serializer;
- no pixel-phase mux.

Two useful timing candidates remain:

```text
800x600 @ 60-ish Hz      40.000 MHz pixel clock     25.00 ns/pixel
800x600 @ 56.25 Hz       36.000 MHz pixel clock     27.78 ns/pixel
```

The 56.25 Hz mode is no longer required by the removed HD6445, but remains attractive because it gives the direct SRAM path another 2.78 ns of cycle time.

A 10 ns SRAM and -10 speed-grade ATF1504/1508-class devices are plausible candidates, but the timing condition is not merely `pixel period - SRAM tAA`.

The path to prove is approximately:

```text
CPLD registered-address tCO
+ PCB address propagation
+ SRAM address-access time
+ PCB data propagation
+ pixel-register setup
< selected address-to-capture interval
```

The implementation should use a true global pixel clock in the CPLD and registered address outputs. Exact timing closure is a part-number/fitter/PCB validation item.

### 10.2 Pixel data stays out of the CPLDs

Per layer:

```text
CPLD A[18:0] -> FRONT SRAM
                    |
                    | D[7:0]
                    v
              pixel register
                    |
                    v
              LAYER_PIXEL[7:0]
```

The SRAM data bus does not enter the layer CPLD. All three pixel registers use one **common capture clock/phase** so the three registered pixels are aligned before composition.

The current register-family candidates are fast 5 V logic such as AHCT or F, subject to exact path timing.

---

## 11. Double buffering and SRAM ownership

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

This removes **time arbitration** between scanout and rendering, but it does **not** eliminate electrical ownership steering.

Each physical SRAM must be connectable either to the layer video address/control source when FRONT or to the RX660 address/data/control path when BACK.

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

The exact steering circuit is still a major open hardware item. `/CE`, `/OE`, `/WE`, address ownership, RX660 data direction, and the video-data selection must all be explicit. Separate packages remove per-cycle arbitration; they do not remove the crossbar function.

---

## 12. Page flipping

The RX660 decides when a rendered back page is complete. Hardware decides when the ownership swap is electrically safe.

Per layer:

```text
RX660 finishes BACK page
        |
        v
sets FLIP_REQUEST
        |
        v
next frame / vertical-blank boundary:
    if FLIP_REQUEST:
        PAGE ^= 1
        clear request
```

Page flipping is **per layer**. If a layer misses its deadline, the previous front page remains visible for another frame.

---

## 13. RX660 register interfaces

### 13.1 Layer CPLDs

The RX660 programs each layer controller through a small write-only register port.

Candidate shared signals:

```text
D0..D7       register data
RS/index     register selection mechanism
/WR          write strobe
```

with one chip select per layer:

```text
/CS_L0
/CS_L1
/CS_L2
```

At minimum the layer register space must support retained START, FLIP_REQUEST, and overlay BLANK where applicable.

### 13.2 Transparency-key register

The compositor has one shared 8-bit transparency-key register:

```text
RX660 D[7:0] -> KEY register -> TRANS_KEY[7:0]
```

The key is normally changed during vertical blank or other controlled display state. The comparator inputs should not be driven directly from casually changing MCU GPIOs.

---

## 14. Scrolling and parallax

Because framebuffer storage is linear and byte-addressed, smooth scrolling is a start-address operation.

```text
scroll right one source pixel:   START += 1
scroll down one source line:     START += 832
```

Each layer owns a separate START register:

```text
L0 START -> distant background
L1 START -> gameplay plane
L2 START -> stationary HUD or independent overlay
```

START should normally be committed at a controlled frame boundary unless an intentional raster effect is desired.

---

## 15. Sprite/object model

There is no dedicated sprite scanout unit.

The RX660 implements objects by drawing them into an overlay framebuffer, normally Layer 1. It performs clipping, masking/transparency, overlap, and pixel writes into the inactive page; the completed page is then flipped into view.

The scanout hardware never needs to know what a sprite or tile is.

---

## 16. Discrete programmable-key compositor

The compositor receives three **already registered** 8-bit layer pixels and produces one registered palette index for the ADV478.

Priority is fixed:

```text
if L2_PIXEL != TRANS_KEY and !BLANK_L2:
    OUT = L2_PIXEL
elif L1_PIXEL != TRANS_KEY and !BLANK_L1:
    OUT = L1_PIXEL
else:
    OUT = L0_PIXEL
```

Layer 0 is always opaque.

### 16.1 Winner detection: two 74x688-class comparators

Each overlay layer is compared against the shared 8-bit transparency key:

```text
                   TRANS_KEY[7:0]
                        |
                        +--------------------+
                        |                    |
                        v                    v
L1_PIXEL[7:0] ------> 74x688             74x688 <------ L2_PIXEL[7:0]
                        |                    |
                      /EQ1                 /EQ2
```

For a 688-style active-low equality output:

```text
/EQ = 0    pixel == TRANS_KEY    -> transparent
/EQ = 1    pixel != TRANS_KEY    -> opaque
```

This polarity is convenient for the mux selection.

With layer blanking:

```text
SEL1 = /EQ1 AND !BLANK_L1
SEL2 = /EQ2 AND !BLANK_L2
```

The blank override can be implemented with a small fast gate package.

### 16.2 Pixel selection: four 74x157-class muxes

A 74x157 is a **4-bit** 2:1 mux, so an 8-bit stage requires two packages.

Stage A selects Layer 0 or Layer 1:

```text
                         SEL1
                           |
                 +---------+---------+
                 |                   |
                 v                   v
            74x157 LOW          74x157 HIGH

L0[3:0] ---> A             L0[7:4] ---> A
L1[3:0] ---> B             L1[7:4] ---> B
/G -------> active         /G --------> active

                 MID[3:0]       MID[7:4]
```

Wire the select sense so:

```text
SEL1 = 0 -> lower layer L0
SEL1 = 1 -> overlay L1
```

Both nibble muxes share the **same** SEL1 because transparency is decided from the complete 8-bit pixel, not nibble-by-nibble.

Stage B is identical:

```text
MID[7:0] versus L2[7:0]
SEL2 = 0 -> MID
SEL2 = 1 -> L2
```

Therefore the mux portion is:

```text
2 x 74x157    L0/L1 8-bit stage
2 x 74x157    MID/L2 8-bit stage
-------------
4 x 74x157 total
```

### 16.3 Registers around the compositor

The compositor is combinational between registered boundaries:

```text
L0 SRAM -> pixel '574 --\
L1 SRAM -> pixel '574 ----> 688 + four 157s -> final '574 -> ADV478
L2 SRAM -> pixel '574 --/
```

The three layer pixel registers share one common capture phase. The final register removes mux/comparator transition glitches before the RAMDAC.

One additional 8-bit register stores `TRANS_KEY[7:0]`.

A literal first-pass discrete implementation is therefore approximately:

```text
3 x 8-bit pixel registers       one per framebuffer layer
2 x 74x688-class comparators    L1/L2 versus TRANS_KEY
4 x 74x157-class muxes          two 8-bit priority stages
1 x small gate package          BLANK overrides
1 x 8-bit final register        compositor -> ADV478
1 x 8-bit key register          RX660 -> TRANS_KEY
```

The exact AHCT/F family choices are a timing decision. The worst combinational path to check is roughly:

```text
layer pixel-register tCO
+ transparency compare
+ first mux stage
+ second mux stage
+ final-register setup
```

---

## 17. Complete Layer 0 leg

Layer 0 is useful as the reference implementation because it contains **one complete framebuffer leg plus the timing master**. Layers 1 and 2 repeat the framebuffer part but consume timing from Layer 0 instead of generating it.

### 17.1 Logical block diagram

```text
                                  PIXEL CLOCK
                                       |
                                       v
                         +---------------------------+
RX660 register bus ----->| L0 ATF1508-class CPLD    |
                         |                           |
                         | START[18:0]               |
                         | ADDR[18:0]                |
                         | PAGE / FLIP_REQUEST       |
                         | H/V raster counters       |
                         | HS / VS / DE              |
                         | line/frame/capture strobes|
                         +-------------+-------------+
                                       |
                              VIDEO_ADDR[18:0]
                                       |
                                       v
                         +---------------------------+
RX660 ADDR[18:0] ------>|                           |
RX660 DATA[7:0] <------>| L0 A/B ownership steering|<---- PAGE
RX660 /RD /WR /CS ----->|                           |
                         +------------+--------------+
                                      |
                         +------------+------------+
                         |                         |
                         v                         v
                    +----------+              +----------+
                    | L0 SRAM A|              | L0 SRAM B|
                    | 512K x 8 |              | 512K x 8 |
                    +----+-----+              +----+-----+
                         |                         |
                         | separate D[7:0] buses   |
                         +------------+------------+
                                      |
                              FRONT-data select
                                      |
                                      v
                              +---------------+
COMMON CAPTURE CLOCK -------->| L0 pixel '574 |
                              +-------+-------+
                                      |
                                L0_PIXEL[7:0]
                                      |
                                      v
                         compositor Stage A
                         lower-layer A input
```

At the same time, the L0 CPLD exports the common raster timing to the rest of the card:

```text
L0 timing master
   |
   +--> HS / VS -> output timing / connector path
   +--> active / line / frame strobes -> L1 and L2 CPLDs
   +--> common pixel-capture phase -> L0/L1/L2 pixel registers
```

### 17.2 L0 scanout operation

For one frame:

```text
1. At frame start:
       ADDR <- START

2. During each active line:
       CPLD presents ADDR[18:0]
       ownership steering routes it to the FRONT SRAM
       SRAM returns one 8-bit palette index
       common capture clock stores that byte in the L0 pixel register
       ADDR advances by one pixel

3. After 800 displayed pixels:
       controller skips the 32-byte hidden tail of the 832-byte pitch

4. Repeat for 600 visible lines.
```

Layer 0's registered pixel is always considered opaque and is the fallback input to compositor Stage A.

### 17.3 L0 rendering operation in parallel

While scanout reads the FRONT package, the RX660 writes the other physical package:

```text
PAGE = A front:
    SRAM A address/control <- video path
    SRAM A data            -> L0 pixel register

    SRAM B address/control <- RX660
    SRAM B data            <-> RX660

PAGE = B front:
    SRAM B address/control <- video path
    SRAM B data            -> L0 pixel register

    SRAM A address/control <- RX660
    SRAM A data            <-> RX660
```

The two SRAM data buses remain physically distinct until explicit steering/select logic. They are not simply tied together.

### 17.4 L0 page flip

```text
RX660 completes back page
        |
        v
writes FLIP_REQUEST_L0
        |
        v
safe frame boundary
        |
        +--> PAGE_L0 toggles
        +--> former BACK becomes FRONT
        +--> former FRONT becomes RX660-owned BACK
        +--> FLIP_REQUEST clears
```

The steering circuit must make the ownership transition break-before-make or otherwise guarantee that no two devices ever drive the same SRAM/data bus simultaneously.

### 17.5 What is physically inside and outside the L0 CPLD

**Inside L0 CPLD:**

```text
START register
live ADDR counter
PAGE / FLIP_REQUEST state
H/V counters and fixed timing decode
frame/line/active strobes
registered VIDEO_ADDR outputs
```

**Outside L0 CPLD:**

```text
A/B address/data/control ownership steering
2 x 512Kx8 framebuffer SRAM
8-bit pixel capture register
compositor
ADV478
```

Pixel data never enters the CPLD.

---

## 18. Raster timing and clocking

Layer 0 contains the only full raster generator.

Two primary timing candidates are useful during bring-up:

```text
800 x 600 @ ~60 Hz      40.000 MHz
800 x 600 @ 56.25 Hz    36.000 MHz
```

The 40 MHz timing is the normal 60 Hz-class target; the 36 MHz mode buys additional SRAM/CPLD/register timing margin without changing the framebuffer architecture.

The master CPLD maintains horizontal and vertical timing counters and derives HS, VS, active-display qualification, and synchronization strobes for the slave layer controllers.

A higher-frequency source or deliberate phase-generation scheme may be used to position address changes, SRAM capture, slave reload strobes, and final RAMDAC registration inside comfortable timing windows. The displayed pixel clock itself remains regular.

---

## 19. Part partition

Current working direction:

```text
L0 / Timing Master   ATF1508AS-AU100 class
L1 controller        ATF1504AS-AU100 class
L2 controller        ATF1504AS-AU100 class
```

The larger L0 part is for macrocell/register pressure, not a fourth logical block.

Each blind controller must retain both START and live ADDR:

```text
19  START bits
19  ADDR bits
 1  PAGE
 1  FLIP_REQUEST
 1  BLANK
 + register-interface/control terms
```

The master additionally needs raster counters and timing state. The fitter decides final device fit.

---

## 20. Electrical design principles

- Three layers use three separate SRAM islands.
- Pixel data does not traverse the CPLDs.
- Video and RX660 normally operate on different physical SRAM packages.
- A/B address/data/control ownership must nevertheless be explicitly steered.
- Page ownership changes only at a controlled boundary.
- No two devices may drive the same SRAM or data bus simultaneously.
- The three pixel registers use one common capture phase.
- The compositor is registered at its input boundaries and again before the RAMDAC.
- Host FIFO overflow is prevented by hardware backpressure.
- Intentional visual glitches may be tolerated for experiments; electrical bus fights are never tolerated.

---

## 21. What this architecture removes from the older design

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
ADDED    discrete programmable-key priority compositor
```

---

## 22. Open questions / validation list

1. **A/B bank steering circuit.** Highest-priority unresolved physical circuit: 19-bit address ownership, RX660 data path, `/CE`, `/OE`, `/WE`, FRONT-data selection, and safe PAGE transition.
2. **Blind-layer fitter result.** Confirm START + ADDR + register decode + PAGE/FLIP/BLANK fits comfortably in ATF1504AS-AU100.
3. **Master fitter result.** Confirm raster counters + L0 framebuffer controller fit comfortably in ATF1508AS-AU100.
4. **Exact SRAM part.** 512Kx8, approximately 10 ns is the conceptual target; exact stocked 5 V part/package remains to be selected.
5. **36/40 MHz direct-fetch timing closure.** Validate CPLD tCO + SRAM tAA + routing + pixel-register setup with exact parts and capture phase.
6. **Common capture-clock implementation.** All three layer pixel registers must capture on one controlled phase.
7. **Master-to-slave timing strobes.** Establish line/frame reload timing with explicit setup margin.
8. **Physical framebuffer geometry.** 832x630 is a strong 512 KiB candidate for an 800x600 viewport but should be frozen only after scroll-margin requirements are agreed.
9. **Register map.** Exact write-only RX660-to-CPLD encoding plus transparency-key write register.
10. **Host FIFO implementation.** Exact glue part and host WAIT/READY wiring.
11. **8-byte transport format.** Exact opcode/data-record semantics.
12. **Compositor timing.** Select exact 688/157/register logic families and verify comparator + two-mux path to the final register.
13. **Transparency-key policy.** Current direction is one global 8-bit key shared by L1/L2; separate keys remain an optional extension.
14. **ADV478 interface timing.** Verify final register edge and RAMDAC setup/hold against the chosen phase.
15. **Primary refresh.** Decide whether the production timing is 40 MHz/60 Hz-class or 36 MHz/56.25 Hz after timing closure and monitor testing.

---

## 23. Current architectural summary

The current Afternoon Latte VDG is:

- one RX660 graphics coprocessor;
- one small host-side FIFO with hardware backpressure;
- fixed 8-byte transport records as the current ingress proposal;
- a larger command ring in RX660 internal SRAM;
- three independent 8-bit indexed framebuffer layers;
- two physical 512Kx8-class SRAM banks per layer as the current page model;
- one CPLD local to each framebuffer layer;
- **three CPLDs total**;
- Layer 0's CPLD is also the sole raster/sync timing master;
- Layer 1 and Layer 2 are timing slaves;
- retained START and live ADDR are separate per layer;
- candidate visible mode 800x600x8bpp;
- candidate physical framebuffer geometry 832x630x8bpp per 512 KiB page;
- one byte/pixel SRAM fetch at pixel rate;
- 40 MHz/60 Hz-class and 36 MHz/56.25 Hz timing remain practical candidates;
- pixel data bypasses the CPLDs and enters common-clocked output registers;
- sprites, tiles, text, and blits are RX660 rendering abstractions;
- independent per-layer scrolling comes from independent START values;
- page flips are RX660-requested and frame-boundary committed;
- Layer 1/2 transparency is a programmable 8-bit palette-index key;
- two 74x688-class comparators decide overlay opacity;
- four 74x157-class nibble muxes implement the two 8-bit priority stages;
- the composited index is registered before the ADV478;
- the complete Layer 0 leg consists of the L0 timing/address CPLD, A/B ownership steering, two SRAM banks, one pixel register, and its input into the compositor;
- the biggest unresolved hardware block is the A/B SRAM ownership steering.

The guiding bias remains:

> **simple deterministic scanout hardware + a capable software-defined renderer, with each electrical responsibility kept local and explicit.**