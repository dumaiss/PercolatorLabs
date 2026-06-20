/*
 * vDrip9958 - Yamaha V9958 video display processor emulation
 *
 * Core: lifecycle, reset, register/status/palette model, CPU ports, and
 * 128 KiB VRAM access. Rendering (Unit 2) and the command engine (Unit 3)
 * live in separate translation units behind the seams declared in
 * vDrip9958_internal.h.
 *
 * Copyright (c) 2026 Virtual Drip contributors
 *
 * This code is licensed under the MIT license. See LICENSE.
 *
 * Behavior is derived from the Yamaha V9938 and V9958 technical manuals
 * (see docs/). Non-obvious decisions reference the relevant behavior in
 * comments. This file contains no assertions and never terminates the host
 * process: all out-of-contract input is handled defensively.
 */

#include "vDrip9958_internal.h"

#include <stdlib.h>
#include <string.h>

/* ==================================================================
 * Register metadata
 * ================================================================== */

/* Supported control registers: R#0..R#27 and R#32..R#46. All other indices
 * are unsupported and writes to them are ignored. */
static bool reg_supported(uint8_t reg)
{
  return (reg <= 27) || (reg >= 32 && reg <= 46);
}

/* Writable bit mask per register. Reserved, meaningless, and V9958-deleted
 * function bits read back as zero. Most supported registers expose all eight
 * bits; the masks below encode the deletions the V9958 makes to the V9938
 * register set. Masks are tightened further as later units implement the
 * features that own each register. */
static uint8_t reg_writable_mask(uint8_t reg)
{
  if (!reg_supported(reg))
  {
    return 0x00;
  }
  switch (reg)
  {
    case 0:  return 0xFE; /* D0 EV (external video) deleted on V9958     */
    case 1:  return 0x7F; /* D7 reserved                                 */
    case 8:  return 0x2F; /* D7 MS, D6 LP, D4 CB deleted (mouse/lightpen/ */
                          /* color-bus external functions)               */
    case 17: return 0xBF; /* D6 reserved in the indirect pointer         */
    default: return 0xFF;
  }
}

/* ==================================================================
 * Reset defaults
 * ================================================================== */

/* Standard MSX2 16-colour palette, three bits per component. */
static const VDrip9958PaletteEntry kMsx2Palette[VDRIP9958_NUM_PALETTE] =
{
  {0, 0, 0}, {0, 0, 0}, {1, 6, 1}, {3, 7, 3},
  {1, 1, 7}, {2, 3, 7}, {5, 1, 1}, {2, 6, 7},
  {7, 1, 1}, {7, 3, 3}, {6, 6, 1}, {6, 6, 4},
  {1, 4, 1}, {6, 2, 5}, {5, 5, 5}, {7, 7, 7}
};

/* ==================================================================
 * VRAM access helpers (shared with rendering/command units)
 * ================================================================== */

uint8_t vdrip9958_vram_read(const VDrip9958* vdp, uint32_t addr)
{
  return vdp->vram[addr & VDRIP9958_VRAM_ADDR_MASK];
}

void vdrip9958_vram_write(VDrip9958* vdp, uint32_t addr, uint8_t value)
{
  vdp->vram[addr & VDRIP9958_VRAM_ADDR_MASK] = value;
}

uint8_t vdrip9958_backdrop_index(const VDrip9958* vdp)
{
  return (uint8_t)(vdp->registers[7] & 0x0F);
}

/* Linear 3-bit -> 8-bit expansion. Unit 2 owns the authoritative color
 * pipeline; this is used only by the Unit 1 rendering placeholder. */
uint32_t vdrip9958_palette_to_rgb(const VDrip9958* vdp, uint8_t index)
{
  const VDrip9958PaletteEntry* e = &vdp->palette[index & 0x0F];
  uint32_t r = (uint32_t)e->r * 255u / 7u;
  uint32_t g = (uint32_t)e->g * 255u / 7u;
  uint32_t b = (uint32_t)e->b * 255u / 7u;
  return (r << 16) | (g << 8) | b;
}

/* ==================================================================
 * Display-mode decoding
 * ================================================================== */

/* Mode select bits, V9938/V9958 layout:
 *   M1 = R#1 D4, M2 = R#1 D3, M3 = R#0 D1, M4 = R#0 D2, M5 = R#0 D3.
 * Combined as (M5 M4 M3 M2 M1). LN (R#9 D7) selects 192/212 lines; IL
 * (R#9 D3) selects interlace. */
void vdrip9958_decode_display(VDrip9958* vdp)
{
  uint8_t r0 = vdp->registers[0];
  uint8_t r1 = vdp->registers[1];
  uint8_t r9 = vdp->registers[9];

  uint8_t m1 = (uint8_t)((r1 >> 4) & 1);
  uint8_t m2 = (uint8_t)((r1 >> 3) & 1);
  uint8_t m3 = (uint8_t)((r0 >> 1) & 1);
  uint8_t m4 = (uint8_t)((r0 >> 2) & 1);
  uint8_t m5 = (uint8_t)((r0 >> 3) & 1);
  uint8_t bits = (uint8_t)((m5 << 4) | (m4 << 3) | (m3 << 2) | (m2 << 1) | m1);

  bool     interlaced = (r9 & 0x08) != 0;
  uint16_t lines      = (r9 & 0x80) ? 212 : 192;

  VDrip9958Mode mode;
  uint16_t      width;

  switch (bits)
  {
    case 0x00: mode = VDRIP9958_MODE_GRAPHIC1;   width = 256; break;
    case 0x01: mode = VDRIP9958_MODE_TEXT1;      width = 256; break;
    case 0x02: mode = VDRIP9958_MODE_MULTICOLOR; width = 256; break;
    case 0x04: mode = VDRIP9958_MODE_GRAPHIC2;   width = 256; break;
    case 0x08: mode = VDRIP9958_MODE_GRAPHIC3;   width = 256; break;
    case 0x09: mode = VDRIP9958_MODE_TEXT2;      width = 512; break;
    case 0x0C: mode = VDRIP9958_MODE_GRAPHIC4;   width = 256; break;
    case 0x10: mode = VDRIP9958_MODE_GRAPHIC5;   width = 512; break;
    case 0x14: mode = VDRIP9958_MODE_GRAPHIC6;   width = 512; break;
    case 0x1C: mode = VDRIP9958_MODE_GRAPHIC7;   width = 256; break;
    default:   mode = VDRIP9958_MODE_INVALID;    width = 0;   break;
  }

  if (mode == VDRIP9958_MODE_INVALID)
  {
    /* Retain the most recent valid geometry/field; report invalid mode. */
    vdp->display.mode = VDRIP9958_MODE_INVALID;
    return;
  }

  vdp->display.mode       = mode;
  vdp->display.width      = width;
  vdp->display.interlaced = interlaced;
  /* Woven interlace doubles the reported output height (384/424); the Unit 2
   * renderer weaves both fields into that many output lines. */
  vdp->display.height     = interlaced ? (uint16_t)(lines * 2) : lines;
  /* Field tracking is advanced by the Unit 2 frame committer; reset baseline
   * is field 0. */
}

/* ==================================================================
 * Register writes (one authoritative path for direct and indirect)
 * ================================================================== */

static void write_register(VDrip9958* vdp, uint8_t reg, uint8_t value)
{
  if (!reg_supported(reg))
  {
    return; /* unsupported register: ignore */
  }

  vdp->registers[reg] = (uint8_t)(value & reg_writable_mask(reg));

  /* Re-decode display metadata; cheap and keeps the snapshot current. */
  vdrip9958_decode_display(vdp);

  /* Command-area registers notify the command engine (inert in Unit 1). */
  if (reg >= 32 && reg <= 46)
  {
    vdrip9958_command_register_written(vdp, reg, vdp->registers[reg]);
  }
}

/* ==================================================================
 * VRAM address handling
 * ================================================================== */

/* True when CPU access is routed to absent expansion VRAM (R#45 D6 MXC).
 * No expansion memory exists: reads return zero, writes are ignored. */
static bool expansion_selected(const VDrip9958* vdp)
{
  return (vdp->registers[45] & 0x40) != 0;
}

/* Increment the 17-bit address and carry into R#14 (A16..A14). */
static void advance_address(VDrip9958* vdp)
{
  vdp->vramAddress = (vdp->vramAddress + 1) & VDRIP9958_VRAM_ADDR_MASK;
  vdp->registers[14] = (uint8_t)((vdp->vramAddress >> 14) & 0x07);
}

/* ==================================================================
 * Lifecycle
 * ================================================================== */

VDrip9958* vDrip9958New(void)
{
  VDrip9958* vdp = (VDrip9958*)malloc(sizeof(VDrip9958));
  if (vdp == NULL)
  {
    return NULL;
  }

  vdp->vram = (uint8_t*)malloc(VDRIP9958_VRAM_SIZE);
  if (vdp->vram == NULL)
  {
    /* Transactional: a partially allocated instance is never returned. */
    free(vdp);
    return NULL;
  }

  vDrip9958Reset(vdp);
  return vdp;
}

void vDrip9958Reset(VDrip9958* vdp)
{
  uint8_t* vram;

  if (vdp == NULL)
  {
    return;
  }

  /* Preserve only the VRAM allocation across the reset. */
  vram = vdp->vram;

  /* Zero everything whose reset value is left undefined by the manuals. */
  memset(vdp, 0, sizeof(VDrip9958));
  vdp->vram = vram;

  /* Documented reset state: all control registers power up to zero,
   * including the V9958 additions R#25..R#27. */

  /* V9958 identification in S#1 (version field, bits D1..D4). */
  vdp->status[1] = VDRIP9958_ID_S1;

  /* Programmable palette: standard MSX2 16-colour set. */
  memcpy(vdp->palette, kMsx2Palette, sizeof(kMsx2Palette));

  /* Clear all 128 KiB of display VRAM. */
  memset(vdp->vram, 0, VDRIP9958_VRAM_SIZE);

  /* Establish a valid baseline display state from the zeroed registers. */
  vdp->display.field = 0;
  vdrip9958_decode_display(vdp);
}

void vDrip9958Destroy(VDrip9958* vdp)
{
  if (vdp == NULL)
  {
    return;
  }
  free(vdp->vram);
  free(vdp);
}

/* ==================================================================
 * Port 1 - control write / status read
 * ================================================================== */

void vDrip9958WriteControl(VDrip9958* vdp, uint8_t value)
{
  if (vdp == NULL)
  {
    return;
  }

  if (!vdp->controlLatch.full)
  {
    /* First write: latch the low address byte or the register value. */
    vdp->controlLatch.value = value;
    vdp->controlLatch.full  = true;
    return;
  }

  /* Second write: interpret the operation, then clear the latch. */
  vdp->controlLatch.full = false;

  if (value & 0x80)
  {
    /* Direct register write: R# in the low 6 bits, data in the latch. */
    write_register(vdp, (uint8_t)(value & 0x3F), vdp->controlLatch.value);
    return;
  }

  /* VRAM address setup. A13..A8 from this byte, A7..A0 from the latch,
   * A16..A14 from R#14. D6 selects write (1) or read (0) mode. */
  {
    uint32_t low14 = (uint32_t)((value & 0x3F) << 8) | vdp->controlLatch.value;
    uint32_t high3 = (uint32_t)(vdp->registers[14] & 0x07);
    vdp->vramAddress = ((high3 << 14) | low14) & VDRIP9958_VRAM_ADDR_MASK;
    vdp->addressWriteMode = (value & 0x40) != 0;

    if (!vdp->addressWriteMode)
    {
      /* Read setup preloads the read-ahead buffer and advances. */
      vdp->readAhead = expansion_selected(vdp)
                         ? 0
                         : vdrip9958_vram_read(vdp, vdp->vramAddress);
      advance_address(vdp);
    }
  }
}

uint8_t vDrip9958ReadStatus(VDrip9958* vdp)
{
  uint8_t sel;
  uint8_t result;

  if (vdp == NULL)
  {
    return 0;
  }

  /* A status read also resets any incomplete control-write sequence. */
  vdp->controlLatch.full = false;

  sel = (uint8_t)(vdp->registers[15] & 0x0F);
  if (sel >= VDRIP9958_NUM_STATUS)
  {
    return 0; /* unsupported status register */
  }

  result = vdp->status[sel];

  /* Documented read-to-clear: S#0 clears the interrupt flag and the
   * 5th-sprite / collision fields after it is read. Those fields are zero
   * in Unit 1 (no interrupts or sprites yet), but the clear is applied so
   * the behavior is correct once Unit 2 sets them. */
  if (sel == 0)
  {
    vdp->status[0] &= (uint8_t)~0xE0;
  }

  /* LMCM uses an S#7 read to clear TR and release the next output pixel. */
  if (sel == 7)
  {
    vdrip9958_command_status_read(vdp, sel);
  }

  return result;
}

/* ==================================================================
 * Port 0 - VRAM data read / write
 * ================================================================== */

void vDrip9958WriteData(VDrip9958* vdp, uint8_t value)
{
  if (vdp == NULL)
  {
    return;
  }

  /* A data-port access cancels any incomplete control sequence. */
  vdp->controlLatch.full = false;

  if (!expansion_selected(vdp))
  {
    vdrip9958_vram_write(vdp, vdp->vramAddress, value);
  }
  /* The written value becomes the next read-ahead, per the port model. */
  vdp->readAhead = value;
  advance_address(vdp);
}

uint8_t vDrip9958ReadData(VDrip9958* vdp)
{
  uint8_t result;

  if (vdp == NULL)
  {
    return 0;
  }

  vdp->controlLatch.full = false;

  /* Return the prefetched byte, then refill from the current address. */
  result = vdp->readAhead;
  vdp->readAhead = expansion_selected(vdp)
                     ? 0
                     : vdrip9958_vram_read(vdp, vdp->vramAddress);
  advance_address(vdp);
  return result;
}

/* ==================================================================
 * Port 2 - palette write
 * ================================================================== */

void vDrip9958WritePalette(VDrip9958* vdp, uint8_t value)
{
  uint8_t index;

  if (vdp == NULL)
  {
    return;
  }

  if (!vdp->paletteLatch.full)
  {
    /* First byte carries red (D6..D4) and blue (D2..D0). */
    vdp->paletteLatch.value = value;
    vdp->paletteLatch.full  = true;
    return;
  }

  /* Second byte carries green (D2..D0). Commit the entry selected by R#16,
   * then advance the index and reset the two-byte phase. */
  index = (uint8_t)(vdp->registers[16] & 0x0F);
  vdp->palette[index].r = (uint8_t)((vdp->paletteLatch.value >> 4) & 0x07);
  vdp->palette[index].b = (uint8_t)(vdp->paletteLatch.value & 0x07);
  vdp->palette[index].g = (uint8_t)(value & 0x07);

  vdp->registers[16] = (uint8_t)((index + 1) & 0x0F);
  vdp->paletteLatch.full = false;
}

/* ==================================================================
 * Port 3 - indirect register write
 * ================================================================== */

void vDrip9958WriteRegisterIndirect(VDrip9958* vdp, uint8_t value)
{
  uint8_t pointer;
  uint8_t target;

  if (vdp == NULL)
  {
    return;
  }

  pointer = vdp->registers[17];
  target  = (uint8_t)(pointer & 0x3F);

  /* R#17 cannot be modified through indirect access. */
  if (target == 17)
  {
    return;
  }

  write_register(vdp, target, value);

  /* Auto-increment unless inhibited by R#17 D7. */
  if ((pointer & 0x80) == 0)
  {
    vdp->registers[17] =
      (uint8_t)((pointer & 0x80) | (uint8_t)((target + 1) & 0x3F));
  }
}

/* ==================================================================
 * Rendering facade (delegates to the Unit 2 seam)
 * ================================================================== */

void vDrip9958ScanLine(VDrip9958* vdp, uint16_t y, uint32_t* pixels)
{
  if (vdp == NULL || pixels == NULL)
  {
    return;
  }
  vdrip9958_render_scanline(vdp, y, pixels);
}

/* ==================================================================
 * Display metadata
 * ================================================================== */

VDrip9958DisplayInfo vDrip9958GetDisplayInfo(const VDrip9958* vdp)
{
  if (vdp == NULL)
  {
    VDrip9958DisplayInfo empty;
    memset(&empty, 0, sizeof(empty));
    empty.mode = VDRIP9958_MODE_INVALID;
    return empty;
  }
  return vdp->display;
}

/* ==================================================================
 * Command facade (delegates to the Unit 3 seam)
 * ================================================================== */

bool vDrip9958StepCommand(VDrip9958* vdp)
{
  if (vdp == NULL)
  {
    return false;
  }
  return vdrip9958_command_step(vdp);
}
