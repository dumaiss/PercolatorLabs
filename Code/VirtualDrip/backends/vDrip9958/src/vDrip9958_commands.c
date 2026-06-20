/*
 * vDrip9958 - Yamaha V9958 video display processor emulation
 *
 * Command engine: deterministic functional execution of the documented
 * V9938/V9958 VRAM commands with observable CE/TR progression and CPU
 * transfers, without VDP clock timing. One natural unit advances per explicit
 * vDrip9958StepCommand() call.
 *
 * Copyright (c) 2026 Virtual Drip contributors
 *
 * This code is licensed under the MIT license. See LICENSE.
 *
 * Reads/writes Unit 1 VRAM through the bounded access helpers and commits
 * effects to Unit 1 registers/status. Does not call the renderer. CE/TR live
 * in S#2 (bit0 = CE, bit7 = TR); BD lives in S#2 bit4. Behavior follows the
 * Yamaha V9938/V9958 command chapter (docs/); coordinate/overlap and the
 * post-command register table are flagged for manual validation.
 */

#include "vDrip9958_internal.h"

/* ==================================================================
 * Register decode helpers
 * ================================================================== */

static uint16_t reg16(const VDrip9958* vdp, int lo, int hi, uint16_t mask)
{
  return (uint16_t)(((uint16_t)vdp->registers[lo] |
                     ((uint16_t)vdp->registers[hi] << 8)) & mask);
}

static void sync_ce_tr(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  if (c->active)        vdp->status[2] |= 0x01; else vdp->status[2] &= (uint8_t)~0x01;
  if (c->transferReady) vdp->status[2] |= 0x80; else vdp->status[2] &= (uint8_t)~0x80;
}

/* ==================================================================
 * Mode-aware format + pixel/byte access
 * ================================================================== */

static void decode_format(const VDrip9958* vdp, VDrip9958CommandState* c)
{
  bool cmd = (vdp->registers[25] & 0x40) != 0;  /* R#25 CMD */
  c->expanded  = false;
  c->supported = true;

  switch (vdp->display.mode)
  {
    case VDRIP9958_MODE_GRAPHIC4:
      c->bitsPerPixel = 4; c->pixelsPerByte = 2; c->colorMask = 0x0F;
      c->pixelsWide = 256; c->bytesPerLine = 128; break;
    case VDRIP9958_MODE_GRAPHIC5:
      c->bitsPerPixel = 2; c->pixelsPerByte = 4; c->colorMask = 0x03;
      c->pixelsWide = 512; c->bytesPerLine = 128; break;
    case VDRIP9958_MODE_GRAPHIC6:
      c->bitsPerPixel = 4; c->pixelsPerByte = 2; c->colorMask = 0x0F;
      c->pixelsWide = 512; c->bytesPerLine = 256; break;
    case VDRIP9958_MODE_GRAPHIC7:
      c->bitsPerPixel = 8; c->pixelsPerByte = 1; c->colorMask = 0xFF;
      c->pixelsWide = 256; c->bytesPerLine = 256; break;
    default:
      if (cmd)
      {
        /* CMD-expanded: Graphic 7 interpretation in any mode. */
        c->expanded = true;
        c->bitsPerPixel = 8; c->pixelsPerByte = 1; c->colorMask = 0xFF;
        c->pixelsWide = 256; c->bytesPerLine = 256;
      }
      else
      {
        c->supported = false;
      }
      break;
  }
}

static uint32_t cmd_addr(const VDrip9958CommandState* c, uint16_t x, uint16_t y)
{
  return ((uint32_t)y * c->bytesPerLine + (x / c->pixelsPerByte))
         & VDRIP9958_VRAM_ADDR_MASK;
}

static uint8_t cmd_read_pixel(const VDrip9958* vdp, const VDrip9958CommandState* c,
                              uint16_t x, uint16_t y, bool expansion)
{
  uint8_t byte;
  if (expansion) return 0;             /* absent expansion VRAM reads zero */
  byte = vdrip9958_vram_read(vdp, cmd_addr(c, x, y));
  switch (c->pixelsPerByte)
  {
    case 2: { int sh = (x & 1) ? 0 : 4; return (uint8_t)((byte >> sh) & 0x0F); }
    case 4: { int sh = (3 - (x & 3)) * 2; return (uint8_t)((byte >> sh) & 0x03); }
    default: return byte;
  }
}

static void cmd_write_pixel(VDrip9958* vdp, const VDrip9958CommandState* c,
                            uint16_t x, uint16_t y, uint8_t val, bool expansion)
{
  uint32_t a;
  uint8_t  byte;
  if (expansion) return;               /* absent expansion VRAM discards */
  a = cmd_addr(c, x, y);
  byte = vdrip9958_vram_read(vdp, a);
  switch (c->pixelsPerByte)
  {
    case 2:
    {
      int sh = (x & 1) ? 0 : 4;
      byte = (uint8_t)((byte & ~(0x0F << sh)) | ((val & 0x0F) << sh));
      break;
    }
    case 4:
    {
      int sh = (3 - (x & 3)) * 2;
      byte = (uint8_t)((byte & ~(0x03 << sh)) | ((val & 0x03) << sh));
      break;
    }
    default:
      byte = val;
      break;
  }
  vdrip9958_vram_write(vdp, a, byte);
}

static uint8_t cmd_read_byte(const VDrip9958* vdp, const VDrip9958CommandState* c,
                             uint16_t x, uint16_t y, bool expansion)
{
  if (expansion) return 0;
  return vdrip9958_vram_read(vdp, cmd_addr(c, x, y));
}

static void cmd_write_byte(VDrip9958* vdp, const VDrip9958CommandState* c,
                           uint16_t x, uint16_t y, uint8_t byte, bool expansion)
{
  if (expansion) return;
  vdrip9958_vram_write(vdp, cmd_addr(c, x, y), byte);
}

/* ==================================================================
 * Logical operation service (pure)
 * ================================================================== */

static uint8_t logical_op(uint8_t logop, uint8_t dst, uint8_t src, uint8_t mask)
{
  bool transparent = (logop & 0x08) != 0;
  uint8_t base = (uint8_t)(logop & 0x07);
  uint8_t out;

  src = (uint8_t)(src & mask);
  if (transparent && src == 0) return dst;  /* leave destination unchanged */

  switch (base)
  {
    case 0: out = src;                    break; /* IMP */
    case 1: out = (uint8_t)(dst & src);   break; /* AND */
    case 2: out = (uint8_t)(dst | src);   break; /* OR  */
    case 3: out = (uint8_t)(dst ^ src);   break; /* XOR */
    case 4: out = (uint8_t)(~src);        break; /* NOT */
    default: return dst;                          /* reserved 5,6,7 */
  }
  return (uint8_t)(out & mask);
}

/* ==================================================================
 * Completion / abort
 * ================================================================== */

static void write_post_command(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  /* Post-command register snapshot (subset of the Yamaha table). */
  vdp->registers[38] = (uint8_t)(c->curDY & 0xFF);
  vdp->registers[39] = (uint8_t)((c->curDY >> 8) & 0x03);
  vdp->registers[42] = (uint8_t)(c->rowsLeft & 0xFF);
  vdp->registers[43] = (uint8_t)((c->rowsLeft >> 8) & 0x03);
}

static void complete_command(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  write_post_command(vdp);
  c->active = false;
  c->transferReady = false;
  c->phase = VDRIP9958_CMD_PHASE_IDLE;
  vdp->registers[46] = (uint8_t)(vdp->registers[46] & 0x0F); /* clear command nibble */
  sync_ce_tr(vdp);
}

static void stop_command(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  /* No work; preserve already-modified VRAM and visible coordinates. */
  c->active = false;
  c->transferReady = false;
  c->phase = VDRIP9958_CMD_PHASE_IDLE;
  vdp->registers[46] = (uint8_t)(vdp->registers[46] & 0x0F);
  sync_ce_tr(vdp);
}

/* ==================================================================
 * Shared rectangle cursor progression
 * ================================================================== */

/* Advance after one processed unit. Returns true when the rectangle is done. */
static bool cursor_advance(VDrip9958CommandState* c, uint16_t step,
                           bool moveSrc, bool moveDst)
{
  if (c->unitsLeft > step) c->unitsLeft = (uint16_t)(c->unitsLeft - step);
  else                     c->unitsLeft = 0;

  if (c->unitsLeft == 0)
  {
    /* Row finished: restore X origins, advance Y, reload the row. */
    c->curSX = c->sx;
    c->curDX = c->dx;
    c->curSY = (uint16_t)(c->curSY + c->diy);
    c->curDY = (uint16_t)(c->curDY + c->diy);
    if (c->rowsLeft > 0) c->rowsLeft--;
    c->unitsLeft = c->nx;
    return c->rowsLeft == 0;
  }

  if (moveSrc) c->curSX = (uint16_t)(c->curSX + c->dix * (int)step);
  if (moveDst) c->curDX = (uint16_t)(c->curDX + c->dix * (int)step);
  return false;
}

/* ==================================================================
 * Packed-byte commands (HMMV, HMMM, YMMM, HMMC)
 * ================================================================== */

static void step_packed(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint16_t step = c->pixelsPerByte;
  bool done;

  switch (c->code)
  {
    case VDRIP9958_CMD_HMMV:
      cmd_write_byte(vdp, c, c->curDX, c->curDY, c->color, c->mxd);
      done = cursor_advance(c, step, false, true);
      if (done) complete_command(vdp);
      return;

    case VDRIP9958_CMD_HMMM:
    {
      uint8_t b = cmd_read_byte(vdp, c, c->curSX, c->curSY, c->mxs);
      cmd_write_byte(vdp, c, c->curDX, c->curDY, b, c->mxd);
      done = cursor_advance(c, step, true, true);
      if (done) complete_command(vdp);
      return;
    }

    case VDRIP9958_CMD_YMMM:
    {
      /* Copy a byte from the source row to the destination row at the same X. */
      uint8_t b = cmd_read_byte(vdp, c, c->curDX, c->curSY, c->mxs);
      cmd_write_byte(vdp, c, c->curDX, c->curDY, b, c->mxd);
      done = cursor_advance(c, step, false, true);
      if (done) complete_command(vdp);
      return;
    }

    case VDRIP9958_CMD_HMMC:
      if (!c->transfer.valid)
      {
        c->transferReady = true;
        c->phase = VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT;
        return;
      }
      cmd_write_byte(vdp, c, c->curDX, c->curDY, c->transfer.latch, c->mxd);
      c->transfer.valid = false;
      done = cursor_advance(c, step, false, true);
      if (done) { complete_command(vdp); return; }
      c->transferReady = true;
      c->phase = VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT;
      return;

    default:
      complete_command(vdp);
      return;
  }
}

/* ==================================================================
 * Logical-pixel commands (LMMV, LMMM, LMMC, LMCM)
 * ================================================================== */

static void step_pixel(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint8_t src, dst, res;
  bool done;

  switch (c->code)
  {
    case VDRIP9958_CMD_LMMV:
      src = (uint8_t)(c->color & c->colorMask);
      dst = cmd_read_pixel(vdp, c, c->curDX, c->curDY, c->mxd);
      res = logical_op(c->logop, dst, src, c->colorMask);
      cmd_write_pixel(vdp, c, c->curDX, c->curDY, res, c->mxd);
      done = cursor_advance(c, 1, false, true);
      if (done) complete_command(vdp);
      return;

    case VDRIP9958_CMD_LMMM:
      src = cmd_read_pixel(vdp, c, c->curSX, c->curSY, c->mxs);
      dst = cmd_read_pixel(vdp, c, c->curDX, c->curDY, c->mxd);
      res = logical_op(c->logop, dst, src, c->colorMask);
      cmd_write_pixel(vdp, c, c->curDX, c->curDY, res, c->mxd);
      done = cursor_advance(c, 1, true, true);
      if (done) complete_command(vdp);
      return;

    case VDRIP9958_CMD_LMMC:
      if (!c->transfer.valid)
      {
        c->transferReady = true;
        c->phase = VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT;
        return;
      }
      src = (uint8_t)(c->transfer.latch & c->colorMask);
      c->transfer.valid = false;
      dst = cmd_read_pixel(vdp, c, c->curDX, c->curDY, c->mxd);
      res = logical_op(c->logop, dst, src, c->colorMask);
      cmd_write_pixel(vdp, c, c->curDX, c->curDY, res, c->mxd);
      done = cursor_advance(c, 1, false, true);
      if (done) { complete_command(vdp); return; }
      c->transferReady = true;
      c->phase = VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT;
      return;

    case VDRIP9958_CMD_LMCM:
      /* VRAM -> CPU: publish one pixel in S#7, wait for the S#7 read. */
      src = cmd_read_pixel(vdp, c, c->curSX, c->curSY, c->mxs);
      vdp->status[7] = (uint8_t)(src & c->colorMask);
      c->transferReady = true;
      done = cursor_advance(c, 1, true, false);
      if (done)
      {
        /* Final value: CE clears but TR/S#7 remain until the read. */
        c->active = false;
        c->phase = VDRIP9958_CMD_PHASE_OUTPUT_PENDING;
      }
      else
      {
        c->phase = VDRIP9958_CMD_PHASE_WAIT_CPU_OUTPUT;
      }
      return;

    default:
      complete_command(vdp);
      return;
  }
}

/* ==================================================================
 * LINE (integer Bresenham)
 * ================================================================== */

static void step_line(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint8_t dst = cmd_read_pixel(vdp, c, c->curDX, c->curDY, c->mxd);
  uint8_t res = logical_op(c->logop, dst, (uint8_t)(c->color & c->colorMask),
                           c->colorMask);
  cmd_write_pixel(vdp, c, c->curDX, c->curDY, res, c->mxd);

  if (c->line.majCount == 0)
  {
    complete_command(vdp);
    return;
  }
  c->line.majCount--;

  /* Advance one unit on the major axis; step the minor axis on error wrap. */
  if (c->maj) c->curDY = (uint16_t)(c->curDY + c->diy);
  else        c->curDX = (uint16_t)(c->curDX + c->dix);

  c->line.err += 2 * (int)c->ny;
  if (c->line.err >= (int)c->nx)
  {
    c->line.err -= 2 * (int)c->nx;
    if (c->maj) c->curDX = (uint16_t)(c->curDX + c->dix);
    else        c->curDY = (uint16_t)(c->curDY + c->diy);
  }
}

/* ==================================================================
 * SRCH
 * ================================================================== */

static void store_search_x(VDrip9958* vdp, uint16_t x)
{
  vdp->status[8] = (uint8_t)(x & 0xFF);
  vdp->status[9] = (uint8_t)((x >> 8) & 0x01);
}

static void step_srch(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint8_t val = cmd_read_pixel(vdp, c, c->curSX, c->curSY, c->mxs);
  uint8_t target = (uint8_t)(c->color & c->colorMask);
  bool match = c->eq ? (val == target) : (val != target);
  int next;

  if (match)
  {
    c->search.found = true;
    vdp->status[2] |= 0x10;          /* BD */
    store_search_x(vdp, c->curSX);
    complete_command(vdp);
    return;
  }

  next = (int)c->curSX + c->dix;
  if (next < 0 || next >= (int)c->pixelsWide)
  {
    c->search.found = false;
    vdp->status[2] &= (uint8_t)~0x10; /* clear BD */
    store_search_x(vdp, c->curSX);
    complete_command(vdp);
    return;
  }
  c->curSX = (uint16_t)next;          /* one compare per step */
}

/* ==================================================================
 * PSET / POINT (single point, complete in one step)
 * ================================================================== */

static void step_pset(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint8_t dst = cmd_read_pixel(vdp, c, c->curDX, c->curDY, c->mxd);
  uint8_t res = logical_op(c->logop, dst, (uint8_t)(c->color & c->colorMask),
                           c->colorMask);
  cmd_write_pixel(vdp, c, c->curDX, c->curDY, res, c->mxd);
  complete_command(vdp);
}

static void step_point(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint8_t val = cmd_read_pixel(vdp, c, c->curSX, c->curSY, c->mxs);
  vdp->status[7] = (uint8_t)(val & c->colorMask);
  complete_command(vdp);
}

/* ==================================================================
 * Command start
 * ================================================================== */

static uint16_t normalize_nx(uint16_t nx) { return nx ? nx : 512; }
static uint16_t normalize_ny(uint16_t ny) { return ny ? ny : 1024; }

static void start_command(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;
  uint8_t r46 = vdp->registers[46];
  uint8_t code = (uint8_t)(r46 >> 4);
  uint8_t arg  = vdp->registers[45];

  if (code == VDRIP9958_CMD_STOP)
  {
    stop_command(vdp);
    return;
  }

  /* Cancel any active command (replacement performs STOP-style cancel). */
  c->active = false;
  c->transferReady = false;

  decode_format(vdp, c);

  c->code  = code;
  c->logop = (uint8_t)(r46 & 0x0F);
  c->sx = reg16(vdp, 32, 33, 0x01FF);
  c->sy = reg16(vdp, 34, 35, 0x03FF);
  c->dx = reg16(vdp, 36, 37, 0x01FF);
  c->dy = reg16(vdp, 38, 39, 0x03FF);
  c->nx = normalize_nx(reg16(vdp, 40, 41, 0x01FF));
  /* LINE uses NY as the minor-axis length, where 0 is a valid axis-aligned
   * line; only rectangle commands normalize a zero NY to the maximum. */
  c->ny = (code == VDRIP9958_CMD_LINE)
            ? reg16(vdp, 42, 43, 0x03FF)
            : normalize_ny(reg16(vdp, 42, 43, 0x03FF));
  c->color = vdp->registers[44];
  c->maj = (arg & 0x01) != 0;
  c->eq  = (arg & 0x02) != 0;
  c->dix = (arg & 0x04) ? -1 : 1;
  c->diy = (arg & 0x08) ? -1 : 1;
  c->mxs = (arg & 0x10) != 0;
  c->mxd = (arg & 0x20) != 0;

  c->curSX = c->sx; c->curSY = c->sy;
  c->curDX = c->dx; c->curDY = c->dy;
  c->unitsLeft = c->nx;
  c->rowsLeft  = c->ny;
  c->transfer.valid = false;
  c->search.found = false;
  c->line.err = 0;
  c->line.majCount = c->nx;

  /* Reserved or mode-unsupported command: complete inertly, CE/TR clear. */
  if (code < VDRIP9958_CMD_POINT || !c->supported)
  {
    c->phase = VDRIP9958_CMD_PHASE_IDLE;
    c->active = false;
    c->transferReady = false;
    vdp->registers[46] = (uint8_t)(vdp->registers[46] & 0x0F);
    sync_ce_tr(vdp);
    return;
  }

  c->active = true;
  c->phase = VDRIP9958_CMD_PHASE_ACTIVE;

  /* CPU-to-VRAM commands take R#44 as the first input value. */
  if (code == VDRIP9958_CMD_HMMC || code == VDRIP9958_CMD_LMMC)
  {
    c->transfer.latch = c->color;
    c->transfer.valid = true;
  }
  sync_ce_tr(vdp);
}

/* ==================================================================
 * Public seams
 * ================================================================== */

void vdrip9958_command_register_written(VDrip9958* vdp, uint8_t reg, uint8_t value)
{
  VDrip9958CommandState* c = &vdp->command;

  if (reg == 46)
  {
    start_command(vdp);
    return;
  }

  /* R#44 write during a CPU-input wait latches one transfer value. */
  if (reg == 44 && c->phase == VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT)
  {
    c->transfer.latch = value;
    c->transfer.valid = true;
    c->transferReady = false;
    c->phase = VDRIP9958_CMD_PHASE_ACTIVE;
    sync_ce_tr(vdp);
  }
}

void vdrip9958_command_status_read(VDrip9958* vdp, uint8_t sel)
{
  VDrip9958CommandState* c = &vdp->command;
  if (sel != 7) return;

  if (c->phase == VDRIP9958_CMD_PHASE_WAIT_CPU_OUTPUT)
  {
    /* Released; the next step prepares the following pixel. */
    c->transferReady = false;
    c->phase = VDRIP9958_CMD_PHASE_ACTIVE;
    sync_ce_tr(vdp);
  }
  else if (c->phase == VDRIP9958_CMD_PHASE_OUTPUT_PENDING)
  {
    /* Final output consumed: command fully completes. */
    c->transferReady = false;
    c->active = false;
    c->phase = VDRIP9958_CMD_PHASE_IDLE;
    vdp->registers[46] = (uint8_t)(vdp->registers[46] & 0x0F);
    sync_ce_tr(vdp);
  }
}

void vdrip9958_command_cpu_write(VDrip9958* vdp, uint8_t value)
{
  /* The command CPU-transfer path uses R#44 writes, not the data port. */
  (void)vdp;
  (void)value;
}

uint8_t vdrip9958_command_cpu_read(VDrip9958* vdp)
{
  /* The command CPU-transfer path uses S#7 reads, not the data port. */
  (void)vdp;
  return 0;
}

bool vdrip9958_command_step(VDrip9958* vdp)
{
  VDrip9958CommandState* c = &vdp->command;

  switch (c->phase)
  {
    case VDRIP9958_CMD_PHASE_IDLE:
      return false;
    case VDRIP9958_CMD_PHASE_WAIT_CPU_INPUT:
    case VDRIP9958_CMD_PHASE_WAIT_CPU_OUTPUT:
      return true;                 /* CE still set; awaiting CPU interaction */
    case VDRIP9958_CMD_PHASE_OUTPUT_PENDING:
      return false;                /* CE clear; awaiting final S#7 read */
    case VDRIP9958_CMD_PHASE_ACTIVE:
    default:
      break;
  }

  switch (c->code)
  {
    case VDRIP9958_CMD_HMMV:
    case VDRIP9958_CMD_HMMM:
    case VDRIP9958_CMD_YMMM:
    case VDRIP9958_CMD_HMMC:
      step_packed(vdp);
      break;
    case VDRIP9958_CMD_LMMV:
    case VDRIP9958_CMD_LMMM:
    case VDRIP9958_CMD_LMMC:
    case VDRIP9958_CMD_LMCM:
      step_pixel(vdp);
      break;
    case VDRIP9958_CMD_LINE:
      step_line(vdp);
      break;
    case VDRIP9958_CMD_SRCH:
      step_srch(vdp);
      break;
    case VDRIP9958_CMD_PSET:
      step_pset(vdp);
      break;
    case VDRIP9958_CMD_POINT:
      step_point(vdp);
      break;
    default:
      complete_command(vdp);
      break;
  }

  sync_ce_tr(vdp);
  return c->active;
}
