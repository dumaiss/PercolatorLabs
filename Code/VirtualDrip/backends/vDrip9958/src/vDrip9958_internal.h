/*
 * vDrip9958 - Yamaha V9958 video display processor emulation
 *
 * Private internal state and subsystem seams.
 *
 * Copyright (c) 2026 Virtual Drip contributors
 *
 * This code is licensed under the MIT license. See LICENSE.
 *
 * WARNING: This header is NOT part of the supported consumer API. Only
 * vDrip9958.h is public. The layout below may change between revisions and
 * is shared only among the vDrip9958 implementation translation units and
 * the project's own white-box smoke tests.
 */

#ifndef VDRIP9958_INTERNAL_H
#define VDRIP9958_INTERNAL_H

#include "vDrip9958.h"

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------
 * Fixed capacities
 * ------------------------------------------------------------------ */
#define VDRIP9958_VRAM_SIZE       131072u   /* fixed 128 KiB display VRAM   */
#define VDRIP9958_VRAM_ADDR_MASK  0x1FFFFu  /* 17 effective address bits    */
#define VDRIP9958_NUM_REGISTERS   64        /* R#0 .. R#63 address space    */
#define VDRIP9958_NUM_STATUS      10        /* S#0 .. S#9                   */
#define VDRIP9958_NUM_PALETTE     16        /* 16 programmable entries      */

/* V9958 status S#1 identification value (chip version field, bits 1..4). */
#define VDRIP9958_ID_S1           0x04

/* ------------------------------------------------------------------
 * Programmable palette entry: three 3-bit components (0..7).
 * ------------------------------------------------------------------ */
typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} VDrip9958PaletteEntry;

/* ------------------------------------------------------------------
 * Two-stage control-port latch (port 1, first/second byte).
 * ------------------------------------------------------------------ */
typedef struct
{
  uint8_t value;  /* first-byte value awaiting the second write */
  bool    full;   /* true when a first byte is latched          */
} VDrip9958ControlLatch;

/* ------------------------------------------------------------------
 * Two-stage palette-port latch (port 2).
 * The first byte carries red (high nibble) and blue (low nibble);
 * the second byte carries green. R#16 selects the target index.
 * ------------------------------------------------------------------ */
typedef struct
{
  uint8_t value;  /* first byte (R/B) awaiting the green byte */
  bool    full;   /* true when the first byte is latched      */
} VDrip9958PaletteLatch;

/* ------------------------------------------------------------------
 * Command-engine state (Unit 3).
 *
 * Established (zero-initialized) by Unit 1 reset and driven by Unit 3. One
 * fixed structure with common fields and dedicated transfer/LINE/SRCH
 * substructures; no allocation and no VRAM ownership.
 * ------------------------------------------------------------------ */

/* Execution phase. */
typedef enum
{
  VDRIP9958_CMD_PHASE_IDLE = 0,
  VDRIP9958_CMD_PHASE_ACTIVE,            /* internal stepping            */
  VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT,    /* HMMC/LMMC awaiting R#44      */
  VDRIP9958_CMD_PHASE_WAIT_CPU_OUTPUT,   /* LMCM awaiting an S#7 read    */
  VDRIP9958_CMD_PHASE_OUTPUT_PENDING     /* LMCM final: CE clear, TR set */
} VDrip9958CommandPhase;

/* Command code: upper nibble of R#46. */
typedef enum
{
  VDRIP9958_CMD_STOP  = 0x0,
  VDRIP9958_CMD_POINT = 0x4,
  VDRIP9958_CMD_PSET  = 0x5,
  VDRIP9958_CMD_SRCH  = 0x6,
  VDRIP9958_CMD_LINE  = 0x7,
  VDRIP9958_CMD_LMMV  = 0x8,
  VDRIP9958_CMD_LMMM  = 0x9,
  VDRIP9958_CMD_LMCM  = 0xA,
  VDRIP9958_CMD_LMMC  = 0xB,
  VDRIP9958_CMD_HMMV  = 0xC,
  VDRIP9958_CMD_HMMM  = 0xD,
  VDRIP9958_CMD_YMMM  = 0xE,
  VDRIP9958_CMD_HMMC  = 0xF
} VDrip9958CommandCode;

typedef struct
{
  /* Common */
  VDrip9958CommandPhase phase;
  bool    active;        /* CE - command in progress (mirrors phase)  */
  bool    transferReady; /* TR - CPU transfer byte ready              */
  uint8_t code;          /* command nibble (R#46 high nibble)         */
  uint8_t logop;         /* logical operation (R#46 low nibble)       */

  /* Parameter snapshot (after zero-count normalization). */
  uint16_t sx, sy, dx, dy, nx, ny;
  uint8_t  color;        /* command color (R#44 at start)             */
  int8_t   dix, diy;     /* direction increments: +1 / -1             */
  bool     maj;          /* LINE major-axis select                    */
  bool     eq;           /* SRCH equality condition                   */
  bool     mxs, mxd;     /* expansion source/destination select       */

  /* Mode-aware format. */
  uint8_t  bitsPerPixel; /* 2 / 4 / 8                                 */
  uint8_t  pixelsPerByte;/* 4 / 2 / 1                                 */
  uint8_t  colorMask;    /* 0x03 / 0x0F / 0xFF                        */
  uint16_t pixelsWide;   /* command surface width                     */
  uint16_t bytesPerLine;
  bool     expanded;     /* CMD-expanded Graphic 7 interpretation     */
  bool     supported;    /* command runs in the current mode          */

  /* Rectangle cursor. */
  uint16_t curSX, curSY, curDX, curDY;
  uint16_t unitsLeft;    /* remaining units in the current row        */
  uint16_t rowsLeft;     /* remaining rows                            */

  /* CPU-transfer substructure (HMMC/LMMC/LMCM). */
  struct
  {
    uint8_t latch;       /* latched input or output value             */
    bool    valid;       /* input value available                     */
  } transfer;

  /* LINE substructure. */
  struct
  {
    int32_t  err;        /* integer Bresenham error                   */
    uint16_t majCount;   /* remaining major-axis steps                */
  } line;

  /* SRCH substructure. */
  struct
  {
    bool found;          /* border/match found                        */
  } search;
} VDrip9958CommandState;

/* ------------------------------------------------------------------
 * Frame-derived rendering state (Unit 2).
 *
 * Persistent state that advances only when the final output scanline of a
 * frame is rendered: the Text 2 blink phase and the next interlace field.
 * Established (zero) by Unit 1 reset and written by the Unit 2 renderer's
 * frame committer.
 * ------------------------------------------------------------------ */
typedef struct
{
  uint8_t blinkOnCount;  /* frames remaining in the ON phase (from R#13)  */
  uint8_t blinkOffCount; /* frames remaining in the OFF phase (from R#13) */
  bool    blinkPhaseOn;  /* current Text 2 / blink ON-phase flag          */
  uint8_t field;         /* next interlace field to display (0 or 1)      */
} VDrip9958FrameState;

/* ------------------------------------------------------------------
 * The complete opaque instance.
 *
 * One aggregate owns every entity. VRAM is a separate heap allocation so
 * the small fixed state and the 128 KiB array are independent.
 * ------------------------------------------------------------------ */
struct VDrip9958_s
{
  /* Control + status register files. Supported writable ranges are
   * R#0..R#27 and R#32..R#46; other indices are read as stored zero. */
  uint8_t registers[VDRIP9958_NUM_REGISTERS];
  uint8_t status[VDRIP9958_NUM_STATUS];

  /* Programmable palette and its power-on/reset defaults are applied by
   * vDrip9958Reset. */
  VDrip9958PaletteEntry palette[VDRIP9958_NUM_PALETTE];

  /* Separate 128 KiB display-VRAM allocation. */
  uint8_t* vram;

  /* CPU-port address state: 17-bit effective display-VRAM address plus the
   * read/write setup direction established by the control port. */
  uint32_t vramAddress;
  bool     addressWriteMode;

  /* Port latches and the one-byte VRAM read-ahead pipeline. */
  VDrip9958ControlLatch controlLatch;
  VDrip9958PaletteLatch paletteLatch;
  uint8_t               readAhead;

  /* Current display metadata snapshot. On an invalid/reserved mode the
   * geometry/field fields retain the most recent valid values while 'mode'
   * reports VDRIP9958_MODE_INVALID. */
  VDrip9958DisplayInfo display;

  /* Reserved command-engine state (Unit 3). */
  VDrip9958CommandState command;

  /* Frame-derived rendering state (Unit 2). */
  VDrip9958FrameState frame;
};

/* ==================================================================
 * Core helpers shared with the rendering and command translation units.
 * Defined in vDrip9958.c.
 * ================================================================== */

/* Bounded VRAM access. The address is masked to the 17-bit display range,
 * so every effective access stays within the fixed allocation. */
uint8_t vdrip9958_vram_read(const VDrip9958* vdp, uint32_t addr);
void    vdrip9958_vram_write(VDrip9958* vdp, uint32_t addr, uint8_t value);

/* Expand a stored palette entry to a 0x00RRGGBB host pixel. Unit 2 owns the
 * authoritative color conversion; this linear expansion is used by the Unit
 * 1 rendering placeholder. */
uint32_t vdrip9958_palette_to_rgb(const VDrip9958* vdp, uint8_t index);

/* Current backdrop/border color index (R#7 low nibble). */
uint8_t vdrip9958_backdrop_index(const VDrip9958* vdp);

/* Re-decode display metadata from the current register state. */
void vdrip9958_decode_display(VDrip9958* vdp);

/* ==================================================================
 * Rendering seam. Defined in vDrip9958_render.c.
 * Unit 1 provides a deterministic border/background placeholder; Unit 2
 * replaces it with real per-mode rendering.
 * ================================================================== */
void vdrip9958_render_scanline(VDrip9958* vdp, uint16_t y, uint32_t* pixels);

/* ==================================================================
 * Command seams. Defined in vDrip9958_commands.c.
 * Unit 1 keeps these inert: no CE/TR change and no VRAM mutation. Unit 3
 * implements the real command engine.
 * ================================================================== */

/* Notification that a command-area register (R#32..R#46) was written.
 * Unit 3 uses an R#46 write to start a command. */
void vdrip9958_command_register_written(VDrip9958* vdp, uint8_t reg, uint8_t value);

/* CPU data interaction with an active transfer command (Unit 3). The V9958
 * command CPU-transfer path uses R#44 writes and S#7 reads, so these data-port
 * seams remain inert; they are retained for completeness. */
void    vdrip9958_command_cpu_write(VDrip9958* vdp, uint8_t value);
uint8_t vdrip9958_command_cpu_read(VDrip9958* vdp);

/* Notification that status register 'sel' was read. LMCM uses an S#7 read to
 * clear TR and release the next output pixel. */
void vdrip9958_command_status_read(VDrip9958* vdp, uint8_t sel);

/* Advance an active command by one logical step. Returns true if a command
 * remains active afterwards. Unit 1: always false. */
bool vdrip9958_command_step(VDrip9958* vdp);

#endif /* VDRIP9958_INTERNAL_H */
