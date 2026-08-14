# Afternoon Latte VDG Architecture

**Status:** evolving design notes  
**Project:** pBITz Afternoon Latte VDG  
**Architecture direction:** RX660-assisted, multi-layer indexed-framebuffer video subsystem

This document captures the current architecture discussion for the Afternoon Latte VDG. It is intentionally a working architecture document rather than a frozen hardware specification. Open questions are called out explicitly so later project discussions can refine the design without treating exploratory ideas as commitments.

---

## 1. Design direction

The VDG started from a tile-engine architecture built around a 6845-family CRTC, tile-map SRAM, pattern SRAM, a serializer, and an indexed-color RAMDAC.

The current direction is to simplify the hardware model around a **single framebuffer architecture** rather than supporting several fundamentally different rendering engines. Tile graphics, sprites, fonts, and other higher-level primitives remain useful, but they are implemented by the RX660 as rendering abstractions over framebuffer layers rather than as separate scanout hardware modes.

The resulting split is deliberate:

- The **host CPU** submits graphics commands and data.
- The **RX660** receives, queues, interprets, and renders those commands.
- The **video pipeline** is deterministic hardware that continuously scans already-rendered framebuffer pages.
- The RX660 is not in the pixel-by-pixel scanout path.

The goal is one graphics architecture that can support games, GUI/development use, scrolling, sprite-like objects, text, and effects without requiring separate tile, sprite, and framebuffer engines.

---

## 2. High-level architecture

```text
 Host CPU
    |
    | fixed-size command records / bulk records
    v
+-------------------+
| Host interface    |
| timing glue       |
| 16-byte FIFO      |
+---------+---------+
          |
          | RX660 drains FIFO rapidly
          v
+-----------------------------+
| RX660                       |
|                             |
| ingress service             |
| internal SRAM command ring  |
| command parser              |
| renderer / blitter          |
| tile / sprite / font APIs   |
| page-flip scheduler         |
+-------------+---------------+
              |
              | external memory writes
              v
       inactive framebuffer pages

                  Scanout side

                       CRTC
                raster/sync timing
                       |
          +------------+------------+
          |            |            |
       Layer 0       Layer 1       Layer 2
       FRONT FB      FRONT FB      FRONT FB
          |            |            |
       pixel 0       pixel 1       pixel 2
          +------------+------------+
                       |
               transparent priority
                    compositor
                       |
                 output register
                       |
                    ADV478
                       |
                      RGB
```

The three display layers operate in parallel and use physically separate framebuffer memory domains. They share raster timing but do not share a scanout data bus.

---

## 3. Host CPU interface

### 3.1 Purpose of the hardware FIFO

The host-to-VDG interface needs timing isolation between comparatively slow retro host buses and the much faster RX660.

The current proposal is a **16-byte hardware FIFO** implemented in the host-interface glue.

The FIFO is not the long-term command queue. Its job is only to provide electrical/timing elasticity at the bus boundary.

The real queue lives in RX660 internal SRAM.

```text
Host bus -> 16-byte hardware FIFO -> RX660 SRAM ring -> command consumer
```

### 3.2 Fixed 8-byte transport records

The current transport proposal uses **fixed 8-byte records**.

A 16-byte FIFO therefore holds exactly two complete records.

A record can represent either a complete small command or one portion of a larger transfer.

A provisional record shape is:

```text
byte 0      opcode / record type
byte 1      flags / layer / subtype
byte 2..7   parameters or payload
```

Examples of operations that may fit in one record include:

- set scroll position;
- request page flip;
- select target layer;
- short drawing/control operations;
- object movement or state changes.

Large transfers are expressed as a sequence of fixed-size records, for example:

```text
UPLOAD_BEGIN
DATA
DATA
DATA
...
UPLOAD_END
```

The exact command encoding remains open, but the fixed-size transport is intended to keep the ingress parser trivial and make complete-record detection unambiguous.

### 3.3 Backpressure

The FIFO must provide hard backpressure to the host.

If the FIFO is full, the host bus cycle is stalled with the appropriate WAIT/READY mechanism until the RX660 drains enough data for the write to complete.

Therefore FIFO depth affects **host stall frequency**, not correctness. No command or payload byte may be dropped because firmware happened to be busy.

The programming interface should not depend on the FIFO being exactly 16 bytes deep; 16 bytes is the current minimum hardware target. A later implementation may use a deeper FIFO without changing the transport protocol.

### 3.4 Useful FIFO status signals

At minimum the glue logic should provide:

- `FIFO_FULL` for host backpressure;
- `FIFO_NOT_EMPTY` for RX660 service.

A useful additional watermark is:

- `FIFO_RECORD_READY` when at least 8 bytes are present.

This permits the RX660 to service complete records rather than being interrupted after the first byte of a host burst.

---

## 4. RX660 ingress and scheduling model

The RX660 is single-core. The architecture is designed so this is not a problem.

The RX660 does **not** need to execute host commands as fast as the host can submit them. It only needs to copy incoming records from the small hardware FIFO into its much larger internal SRAM queue quickly enough to avoid unnecessary host stalls.

The ingress path should therefore do as little work as possible:

```text
while FIFO has data:
    copy byte/record into SRAM ring
return
```

Command parsing and rendering occur later from the SRAM queue.

Conceptually the RX660 alternates between two kinds of work:

```text
[GULP incoming records]
        |
[consume/render queued work]
        |
[GULP incoming records]
        |
[continue rendering]
```

Ingress has bounded latency and higher priority than long-running rendering work.

Rendering operations must therefore be interruptible or chunked so a large blit/fill cannot block FIFO servicing for an unbounded period.

### 4.1 Three deliberately decoupled rates

The system separates three rates:

1. **Host production rate** — Host CPU to hardware FIFO.
2. **Ingress rate** — Hardware FIFO to RX660 internal SRAM.
3. **Execution rate** — RX660 command queue to framebuffer rendering.

```text
Host -> small FIFO -> SRAM queue -> renderer
       timing         burst        variable
       elasticity     absorption   execution cost
```

The SRAM queue may be several KiB or larger. At 8 bytes per record, even a 4 KiB queue stores 512 records.

---

## 5. Rendering model

The RX660 is the graphics coprocessor.

It consumes queued host commands and renders them into **inactive framebuffer pages**. It may implement operations such as:

- fills;
- copies/blits;
- masked blits;
- sprite/object drawing;
- tile drawing from an atlas;
- font/text rendering;
- image decompression or RLE expansion;
- dirty-rectangle updates;
- scrolling control;
- palette management;
- page-flip requests.

This keeps useful retro-style APIs without requiring dedicated tile or sprite scanout hardware.

For example, a host command such as:

```text
DRAW_TILE(layer, tile_id, x, y)
```

means that the RX660 copies the corresponding tile pixels into the selected layer's back framebuffer. The display hardware itself has no concept of a tile.

Likewise, a sprite command is an RX660 rendering abstraction rather than a separate sprite evaluator in the video pipeline.

---

## 6. Three framebuffer layers

The current architecture uses **three complete display layers**.

Natural software conventions are:

- **Layer 0:** world/background;
- **Layer 1:** objects / sprite-like content / gameplay foreground;
- **Layer 2:** HUD / UI / pointer / top overlay.

These roles are not fixed in hardware. Applications may instead use the layers for parallax backgrounds, windows, foreground scenery, debugging overlays, or any other purpose.

### 6.1 Indexed pixel format

The current preferred framebuffer model is **8-bit indexed color**.

Each framebuffer byte is directly an ADV478 palette index:

```text
framebuffer byte -> IDX[7:0] -> ADV478 -> RGB
```

This deliberately avoids recreating the older tile engine's split `ATTR[3:0] + PIX[3:0]` representation in framebuffer memory.

For overlay layers, palette index `0x00` is the natural transparency key.

Layer 0 may treat index zero as an ordinary opaque background color.

### 6.2 Pixel compositor

Each layer independently produces one registered 8-bit pixel for the current raster position.

The final compositor is intentionally simple:

```text
if L2_PIXEL != 0:
    OUT = L2_PIXEL
else if L1_PIXEL != 0:
    OUT = L1_PIXEL
else:
    OUT = L0_PIXEL
```

The selected 8-bit index is registered and sent to the ADV478 RAMDAC.

This is the only shared per-pixel composition logic required for the basic three-layer design.

---

## 7. Physical framebuffer memory organization

### 7.1 Separate memory per layer

The scanout memories are **physically separated per layer** rather than packed into one shared VRAM pool.

This is an electrical simplification, not a capacity optimization.

Each layer has its own local SRAM, pixel latch, and address path. All three layers can therefore fetch pixels simultaneously without multiplexing a shared data bus or arbitrating scanout bandwidth among layers.

```text
same raster position
       |
   +---+---+
   |   |   |
  L0  L1  L2
 SRAM SRAM SRAM
   |   |   |
 pixel pixel pixel
```

Adding layers consumes additional silicon and PCB area, but does not multiply the scanout latency because the three memory paths operate in parallel.

### 7.2 Double buffering

Each layer is double-buffered.

Conceptually:

```text
Layer 0A / Layer 0B
Layer 1A / Layer 1B
Layer 2A / Layer 2B
```

The preferred electrical organization is for the front and back pages to be physically distinct SRAM banks within each layer.

During normal operation:

- scanout owns the FRONT bank;
- the RX660 owns the BACK bank.

At vertical blank the banks exchange roles.

This almost completely removes normal video/RX660 memory arbitration: rendering and scanout ordinarily occur against different physical devices.

### 7.3 Candidate memory sizing

Exact SRAM sizing is not yet frozen.

A particularly attractive target is enough memory per layer for two full 8bpp framebuffer pages. For example, an 800x600x8bpp framebuffer occupies 480,000 bytes; two pages occupy 960,000 bytes, fitting naturally within 1 MiB per layer.

This suggests a possible implementation using two approximately 512 KiB banks per layer, but the final geometry, bus width, package count, and resolution target remain open hardware decisions.

---

## 8. Page flipping

The RX660 should request display flips rather than directly switching visible memory at an arbitrary instant.

A CPLD/glue register set can contain something equivalent to:

```text
NEXT_PAGE
FLIP_PENDING
```

At the safe vertical-blank boundary:

```text
if VBLANK && FLIP_PENDING:
    DISPLAY_PAGE = NEXT_PAGE
```

The normal API therefore produces tear-free frame changes.

A global page bit that flips all three layers atomically is the simplest initial implementation. Independent layer page selection may be useful later, but is not required for the first version.

If rendering of a new frame misses its deadline, the RX660 simply does not request the flip; the previous front page remains visible for another frame.

---

## 9. CRTC role

The 6345/6445-class CRTC is retained primarily as a **programmable raster and sync generator**.

In the framebuffer architecture it no longer needs to implement tile addressing semantics.

Its useful outputs include:

- horizontal sync;
- vertical sync;
- display enable;
- character/raster progression from which the external pixel-position logic can derive or synchronize X/Y timing.

The framebuffer address generator is implemented externally in the video logic rather than forcing framebuffer storage into the CRTC's native character/tile memory model.

This makes the graphics architecture independent of the historical CGA-style trick of using CRTC raster-address bits as framebuffer bank/address bits, although that technique remains conceptually related.

---

## 10. Per-layer framebuffer address generation

Each layer has its own scanout address generator but shares the same raster timing.

Useful per-layer registers are expected to include:

```text
BASE / page select
PITCH
SCROLL_X
SCROLL_Y
```

Conceptually:

```text
address = BASE
        + (Y + SCROLL_Y) * PITCH
        + (X + SCROLL_X)
```

No hardware multiplier is required in the pixel path. The implementation can maintain line and pixel address accumulators:

```text
start of frame:
    LINE_BASE = BASE + initial vertical offset

start of line:
    ADDR = LINE_BASE + horizontal offset

each pixel/word:
    ADDR += increment

end of line:
    LINE_BASE += PITCH
```

This model permits arbitrary pixel-level horizontal and vertical scrolling without tile-boundary logic.

---

## 11. Smooth scrolling and parallax

Framebuffer scanout makes fine scrolling a native address-generation operation rather than a special tile-engine feature.

Each layer may have independent `SCROLL_X` and `SCROLL_Y` values.

This naturally supports parallax. For example:

```text
Layer 0  distant background, slow scroll
Layer 1  gameplay plane, normal scroll
Layer 2  HUD, no scroll
```

The earlier tile-engine proposal required coarse tile offsets plus fine 0..7-pixel selection and current/next-tile windows for smooth horizontal scrolling. That machinery is unnecessary in the framebuffer-only scanout architecture.

---

## 12. Sprite/object model

Dedicated sprite scanout hardware is not currently required.

The RX660 implements sprite-like objects by drawing them into an overlay layer's inactive framebuffer page, normally Layer 1.

A moving object therefore becomes an RX660 operation such as:

```text
SPRITE_DRAW(layer=1, object, x, y)
```

The RX660 performs clipping, transparency/masking, and pixel writes into the back page. At VBlank the complete layer is flipped into view.

The scanout hardware sees only ordinary framebuffer pixels. There is no sprite-per-line limit, sprite evaluator, background restore problem, or tile palette-bank conflict.

Layer 2 may independently carry UI, cursor, debug, or additional foreground graphics.

---

## 13. Framebuffer fetch path

For one layer, the real-time display path is intentionally short:

```text
raster position
     |
framebuffer address generator
     |
front SRAM
     |
pixel/word latch
     |
layer pixel[7:0]
```

With a 16-bit SRAM organization, one read returns two 8-bit pixels. A byte-select mux can then emit the two pixels on successive pixel clocks.

```text
16-bit SRAM word
+----------+----------+
| pixel N  | pixel N+1|
+----------+----------+
      |
   word latch
      |
 byte select
      |
 8-bit layer pixel
```

This allows the SRAM word-fetch rate to be approximately half the pixel rate.

All three layers perform these accesses concurrently through their independent SRAM domains.

---

## 14. Clocking and pipeline timing

The high-frequency master/video clock remains useful even though the tile serializer is removed.

Its job is to provide deterministic internal phases for operations such as:

```text
phase 0   present framebuffer address
phase 1   allow SRAM access time
phase 2   latch returned word
phase 3   prepare next address / control transition
```

The external pixel clock itself should remain regular. Fine scroll and layer positioning are achieved through address generation, not by stretching, suppressing, or irregularly shifting pixel-clock pulses.

Timing-phase taps may be exposed or jumper-selectable during hardware characterization so latch/control transitions can be moved within the available SRAM timing window without a PCB respin.

---

## 15. Normal host-to-screen transaction

A typical object draw illustrates the separation of responsibilities.

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
internal SRAM command queue
   |
   v
RX660 command consumer / renderer
   |
   | writes inactive Layer 1 page
   v
Layer 1 BACK SRAM
```

At the same time, independently:

```text
CRTC raster timing
      |
      +-> Layer 0 FRONT SRAM -> L0 pixel --+
      +-> Layer 1 FRONT SRAM -> L1 pixel --+-> compositor -> ADV478 -> RGB
      +-> Layer 2 FRONT SRAM -> L2 pixel --+
```

At vertical blank, the requested front/back bank swap is committed and the new image becomes visible.

The key architectural boundary is therefore:

> The host-to-RX660 side is a command-driven graphics renderer; the SRAM-to-RAMDAC side is a simple deterministic display engine. They meet at the framebuffer page boundary.

---

## 16. Relationship to the earlier tile-engine design

The earlier VDG architecture used:

```text
CRTC -> tile map -> pattern memory -> serializer -> palette index
```

and contemplated multiple hardware modes, including tile-oriented modes and a framebuffer-like mode derived from CRTC raster addressing.

The current direction is instead:

```text
CRTC timing -> framebuffer address -> pixel -> compositor -> palette index
```

Reasons for preferring one framebuffer architecture include:

- simpler scanout hardware;
- one programming/rendering model across resolutions;
- straightforward pixel-level scrolling;
- straightforward layer compositing;
- sprite/object support without a dedicated sprite engine;
- no tile-specific palette-bank limitation;
- easier double buffering;
- clearer separation between rendering and display;
- the RX660 is powerful enough to provide tile/sprite/font abstractions in firmware.

Tile-oriented commands remain valuable because they reduce host traffic and simplify game software, but they no longer imply tile-oriented display hardware.

---

## 17. Modes versus timing profiles

The current preference is to avoid multiple fundamentally different graphics modes.

Different screen resolutions may still be supported as **timing profiles** if useful. Changing a timing profile can alter:

- CRTC timing registers;
- pixel clock;
- visible width/height;
- framebuffer pitch;
- allowable framebuffer/page geometry.

It should not change the fundamental rendering or scanout architecture.

A candidate 800x600x8bpp profile is attractive because a double-buffered layer fits within about 1 MiB, but the final supported timing set is not yet frozen.

---

## 18. Electrical and safety principles

The VDG should permit intentional visual abuse without permitting electrical contention.

Normal rules include:

- scanout and rendering use different physical banks whenever possible;
- page ownership changes only at a controlled boundary;
- no two devices are allowed to drive the same bus simultaneously;
- host FIFO overflow is prevented by hardware backpressure;
- visible corruption caused by deliberately racing display updates may be acceptable in debug/demo scenarios, but bus fights are never acceptable.

---

## 19. Current open questions

The following items remain intentionally unfrozen:

1. Exact host-interface glue device and FIFO implementation.
2. Exact 8-byte record encoding and command set.
3. Whether bulk upload data shares the same record stream or receives a separate logical/physical channel.
4. RX660 SRAM ring size and scheduling policy.
5. Exact external SRAM part, width, and bank organization per layer.
6. Final framebuffer capacity per layer.
7. Final primary video timing/resolution; 800x600x8bpp is currently an attractive candidate, not a commitment.
8. Whether all three layers always use identical pixel formats.
9. Whether page flips are initially global-only or independently selectable per layer.
10. Exact transparent-index and palette conventions.
11. Exact CRTC-to-X/Y/address-generator implementation.
12. Exact high-speed clock frequency, divider tree, and phase-selection mechanism.
13. Whether the RX660 receives HS/VS interrupts for effects/scheduling beyond VBlank page management.
14. Final rendering command semantics for sprites, tiles, fonts, blits, and dirty-region handling.

---

## 20. Current architectural summary

The present Afternoon Latte VDG concept is:

- one 6345/6445-family CRTC used primarily for raster timing;
- one RX660 graphics coprocessor;
- a small hardware FIFO between host and RX660;
- fixed-size 8-byte transport records as the current ingress proposal;
- a larger command queue in RX660 internal SRAM;
- three independent 8-bit indexed framebuffer layers;
- physically separate VRAM domains per layer;
- front/back framebuffer banks for double buffering;
- RX660 ownership of inactive pages while video scans active pages;
- independent per-layer base/pitch/scroll state;
- hardware pixel-level scrolling through framebuffer address generation;
- sprites, tiles, fonts, and blits implemented by RX660 firmware;
- transparent priority composition of Layers 2, 1, and 0;
- ADV478 indexed-color RAMDAC output;
- a high-speed phased clock used to sequence deterministic SRAM/latch timing;
- page changes committed at VBlank.

The architecture is intentionally biased toward **simple deterministic scanout hardware plus a capable software-defined rendering front end**, rather than multiple specialized video engines.
