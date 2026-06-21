/*
 * vDrip9958 - Yamaha V9958 video display processor emulation
 *
 * Rendering: caller-selected native-size RGB scanlines for all documented
 * display modes, V9958 color systems (programmable palette, direct RGB, YJK,
 * YAE), horizontal scrolling, woven interlace, and both sprite modes, plus
 * line- and frame-derived status effects.
 *
 * Copyright (c) 2026 Virtual Drip contributors
 *
 * This code is licensed under the MIT license. See LICENSE.
 *
 * The renderer reads authoritative Unit 1 state (registers, palette, VRAM,
 * status) and never mutates VRAM or invokes the command engine. It does not
 * own the caller's buffer. Behavior follows the Yamaha V9938/V9958 manuals
 * (docs/); table-addressing and bitmap-page formulas are the areas most worth
 * manual validation and are flagged in the implementation summary.
 */

#include "vDrip9958_internal.h"

/* ==================================================================
 * Local rendering types (one scanline call; never persistent/public)
 * ================================================================== */

typedef enum
{
  PF_PATTERN,   /* 1bpp pattern + color table (Text/Graphic1-3)        */
  PF_TEXT,      /* 6-pixel text cells with R#7/R#12 colors             */
  PF_MULTICOLOR,
  PF_BPP4,      /* Graphic 4 / Graphic 6 packed 4-bit palette codes    */
  PF_BPP2,      /* Graphic 5 packed 2-bit palette codes                */
  PF_DIRECT,    /* Graphic 7 direct 8-bit RGB (GRB332) / YJK / YAE     */
  PF_BORDER     /* invalid mode: deterministic border fill             */
} PixelFormat;

typedef struct
{
  VDrip9958Mode mode;
  PixelFormat   format;
  uint16_t      width;        /* natural output width 256/512            */
  uint16_t      baseHeight;   /* 192/212 field height                    */
  uint16_t      outputHeight; /* doubled for woven interlace             */
  bool          interlaced;
  bool          blanked;
  uint8_t       spriteMode;   /* 0 (none/text), 1, or 2                  */
  uint32_t      borderRgb;

  /* Table / page base inputs. */
  uint32_t nameBase;
  uint32_t patternBase;
  uint32_t colorBase;
  uint32_t bitmapBase;
  uint32_t spriteAttrBase;
  uint32_t spritePatternBase;

  /* Horizontal scroll, decoded units. */
  uint16_t coarse;            /* in coarse units (mode-dependent size)   */
  uint8_t  fine;              /* 0..7                                     */
  uint8_t  coarseUnit;        /* 8 normal, 16 G5/G6                       */
  uint8_t  fineUnit;          /* 1 normal, 2 G5/G6                        */
  uint8_t  maskPixels;        /* MSK left-edge pixels (0/8/16)           */

  bool     yjk;
  bool     yae;
  bool     blinkOn;           /* current Text 2 blink phase              */
} Descriptor;

/* Accumulated status effects from rendering one line. Merged (OR) into the
 * Unit 1 status file without clearing prior unread flags. */
typedef struct
{
  bool    collision;
  uint8_t collisionX, collisionY;
  bool    overflow;
  uint8_t overflowSprite;
  bool    horizontalInt;
  bool    verticalInt;
  bool    frameComplete;
} LineStatus;

#define MAX_LINE_PIXELS  512
#define MAX_SPRITES      32

/* ==================================================================
 * Integer color services (pure; never touch status)
 * ================================================================== */

static uint32_t pack_rgb(uint32_t r, uint32_t g, uint32_t b)
{
  return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

static uint32_t expand3(uint8_t c)  /* 0..7  -> 0..255, rounded */
{
  return ((uint32_t)(c & 7) * 255u + 3u) / 7u;
}

static uint32_t expand2(uint8_t c)  /* 0..3  -> 0..255, rounded */
{
  return ((uint32_t)(c & 3) * 255u + 1u) / 3u;
}

static uint32_t expand5(uint8_t c)  /* 0..31 -> 0..255, rounded */
{
  return ((uint32_t)(c & 31) * 255u + 15u) / 31u;
}

/* Programmable palette entry (3-bit components) to host RGB. */
static uint32_t palette_rgb(const VDrip9958* vdp, uint8_t index)
{
  const VDrip9958PaletteEntry* e = &vdp->palette[index & 0x0F];
  return pack_rgb(expand3(e->r), expand3(e->g), expand3(e->b));
}

/* Graphic 7 direct color byte: %GGG RRR BB (3-bit G/R, 2-bit B). */
static uint32_t g7_direct_rgb(uint8_t c)
{
  uint8_t g = (uint8_t)((c >> 5) & 0x07);
  uint8_t rr = (uint8_t)((c >> 2) & 0x07);
  uint8_t b = (uint8_t)(c & 0x03);
  return pack_rgb(expand3(rr), expand3(g), expand2(b));
}

/* Clamp a signed value to 0..31. */
static uint8_t clamp5(int v)
{
  if (v < 0)  return 0;
  if (v > 31) return 31;
  return (uint8_t)v;
}

/* ==================================================================
 * Display descriptor decode (per scanline call)
 * ================================================================== */

static void decode_descriptor(const VDrip9958* vdp, Descriptor* d)
{
  uint8_t r1 = vdp->registers[1];
  uint8_t r9 = vdp->registers[9];
  uint8_t r25 = vdp->registers[25];

  d->mode         = vdp->display.mode;
  d->width        = vdp->display.width ? vdp->display.width : 256;
  d->baseHeight   = (r9 & 0x80) ? 212 : 192;
  d->interlaced   = (r9 & 0x08) != 0;
  d->outputHeight = d->interlaced ? (uint16_t)(d->baseHeight * 2) : d->baseHeight;
  d->blanked      = (r1 & 0x40) == 0;          /* BL: display enable */
  d->yjk          = (r25 & 0x08) != 0;          /* YJK */
  d->yae          = (r25 & 0x10) != 0;          /* YAE */

  /* Table / page bases. */
  d->nameBase          = (uint32_t)(vdp->registers[2] & 0x7F) << 10;
  d->patternBase       = (uint32_t)(vdp->registers[4] & 0x3F) << 11;
  d->colorBase         = ((uint32_t)(vdp->registers[10] & 0x07) << 14)
                       | ((uint32_t)vdp->registers[3] << 6);
  d->spriteAttrBase    = ((uint32_t)(vdp->registers[11] & 0x03) << 15)
                       | ((uint32_t)vdp->registers[5] << 7);
  d->spritePatternBase = (uint32_t)(vdp->registers[6] & 0x3F) << 11;

  /* Border color: G7 direct uses R#7 as a direct byte; others use the
   * backdrop palette index. */
  /* Format, sprite mode, bitmap base, and scroll units per mode. */
  d->coarseUnit = 8;
  d->fineUnit   = 1;
  d->spriteMode = 0;
  d->bitmapBase = 0;

  switch (d->mode)
  {
    case VDRIP9958_MODE_TEXT1:
    case VDRIP9958_MODE_TEXT2:
      d->format = PF_TEXT;
      d->spriteMode = 0; /* no sprites in text modes */
      break;
    case VDRIP9958_MODE_MULTICOLOR:
      d->format = PF_MULTICOLOR;
      d->spriteMode = 1;
      break;
    case VDRIP9958_MODE_GRAPHIC1:
    case VDRIP9958_MODE_GRAPHIC2:
      d->format = PF_PATTERN;
      d->spriteMode = 1;
      break;
    case VDRIP9958_MODE_GRAPHIC3:
      d->format = PF_PATTERN;
      d->spriteMode = 2;
      break;
    case VDRIP9958_MODE_GRAPHIC4:
      d->format = PF_BPP4;
      d->spriteMode = 2;
      d->bitmapBase = (uint32_t)(vdp->registers[2] & 0x60) << 10;
      break;
    case VDRIP9958_MODE_GRAPHIC5:
      d->format = PF_BPP2;
      d->spriteMode = 2;
      d->bitmapBase = (uint32_t)(vdp->registers[2] & 0x60) << 10;
      d->coarseUnit = 16;
      d->fineUnit   = 2;
      break;
    case VDRIP9958_MODE_GRAPHIC6:
      d->format = PF_BPP4;
      d->spriteMode = 2;
      d->bitmapBase = (uint32_t)(vdp->registers[2] & 0x20) << 11;
      d->coarseUnit = 16;
      d->fineUnit   = 2;
      break;
    case VDRIP9958_MODE_GRAPHIC7:
      d->format = PF_DIRECT;
      d->spriteMode = 2;
      d->bitmapBase = (uint32_t)(vdp->registers[2] & 0x20) << 11;
      break;
    default:
      d->format = PF_BORDER;
      d->spriteMode = 0;
      break;
  }

  if (d->mode == VDRIP9958_MODE_GRAPHIC7 && !d->yjk)
  {
    d->borderRgb = g7_direct_rgb(vdp->registers[7]);
  }
  else
  {
    d->borderRgb = palette_rgb(vdp, (uint8_t)(vdp->registers[7] & 0x0F));
  }

  /* Horizontal scroll (R#26 coarse, R#27 fine) and MSK (R#25 bit1). */
  d->coarse     = (uint16_t)(vdp->registers[26] & 0x3F);
  d->fine       = (uint8_t)(vdp->registers[27] & 0x07);
  d->maskPixels = (r25 & 0x02) ? d->coarseUnit : 0;

  /* Text 2 blink phase: OFF==0 means always-on. */
  if ((vdp->registers[13] & 0x0F) == 0)
  {
    d->blinkOn = true;
  }
  else
  {
    d->blinkOn = vdp->frame.blinkPhaseOn;
  }
}

/* Map a destination X to a scrolled source X within the page width. */
static uint16_t scroll_source_x(const Descriptor* d, uint16_t x)
{
  int offset = (int)d->coarse * d->coarseUnit - (int)d->fine * d->fineUnit;
  int sx = (int)x + offset;
  int w  = (int)d->width;
  sx %= w;
  if (sx < 0) sx += w;
  return (uint16_t)sx;
}

/* ==================================================================
 * Background mode renderers
 * Each writes only within [0, width); borders/blank remain border color.
 * ================================================================== */

static void render_text(const VDrip9958* vdp, const Descriptor* d,
                        uint16_t sy, uint32_t* px)
{
  bool text2 = (d->mode == VDRIP9958_MODE_TEXT2);
  int  cols  = text2 ? 80 : 40;
  int  cellRow = sy & 7;
  uint32_t fg = palette_rgb(vdp, (uint8_t)((vdp->registers[7] >> 4) & 0x0F));
  uint32_t bg = palette_rgb(vdp, (uint8_t)(vdp->registers[7] & 0x0F));
  uint32_t blinkFg = palette_rgb(vdp, (uint8_t)((vdp->registers[12] >> 4) & 0x0F));
  uint32_t blinkBg = palette_rgb(vdp, (uint8_t)(vdp->registers[12] & 0x0F));
  uint32_t nameRow = d->nameBase + (uint32_t)(sy >> 3) * (uint32_t)cols;
  /* Centre the active area inside the natural width (documented borders). */
  int border = (int)((d->width - (uint16_t)(cols * 6)) / 2);
  int c;

  for (c = 0; c < cols; ++c)
  {
    uint8_t name = vdrip9958_vram_read(vdp, nameRow + (uint32_t)c);
    uint8_t bits = vdrip9958_vram_read(vdp,
                     d->patternBase + (uint32_t)name * 8u + (uint32_t)cellRow);
    uint32_t useFg = fg, useBg = bg;
    int b;

    if (text2)
    {
      /* Per-character blink attribute from the color table (1 bit/char). */
      uint32_t attrAddr = d->colorBase + (uint32_t)((sy >> 3) * cols + c) / 8u;
      uint8_t  attr = vdrip9958_vram_read(vdp, attrAddr);
      if ((attr >> (7 - ((c) & 7))) & 1)
      {
        if (d->blinkOn) { useFg = blinkFg; useBg = blinkBg; }
      }
    }

    for (b = 0; b < 6; ++b)
    {
      int dx = border + c * 6 + b;
      if (dx >= 0 && dx < (int)d->width)
      {
        px[dx] = ((bits >> (7 - b)) & 1) ? useFg : useBg;
      }
    }
  }
}

static void render_pattern(const VDrip9958* vdp, const Descriptor* d,
                           uint16_t sy, uint32_t* px)
{
  int  cellRow = sy & 7;
  uint32_t nameRow = d->nameBase + (uint32_t)(sy >> 3) * 32u;
  bool g23 = (d->mode == VDRIP9958_MODE_GRAPHIC2 ||
              d->mode == VDRIP9958_MODE_GRAPHIC3);
  /* Graphic 2/3 split VRAM into three vertical thirds selected by R#4/R#3. */
  uint32_t bank = g23 ? ((uint32_t)(sy >> 6) & 0x03) * 0x800u : 0u;
  int col;

  for (col = 0; col < 32; ++col)
  {
    uint8_t name = vdrip9958_vram_read(vdp, nameRow + (uint32_t)col);
    uint32_t patAddr;
    uint32_t colAddr;
    uint8_t bits, color;
    uint32_t fg, bgc;
    int b;

    if (g23)
    {
      patAddr = d->patternBase + bank + (uint32_t)name * 8u + (uint32_t)cellRow;
      colAddr = d->colorBase   + bank + (uint32_t)name * 8u + (uint32_t)cellRow;
    }
    else /* Graphic 1: shared 8-entry color table */
    {
      patAddr = d->patternBase + (uint32_t)name * 8u + (uint32_t)cellRow;
      colAddr = d->colorBase   + (uint32_t)name / 8u;
    }
    bits  = vdrip9958_vram_read(vdp, patAddr);
    color = vdrip9958_vram_read(vdp, colAddr);
    fg  = palette_rgb(vdp, (uint8_t)((color >> 4) & 0x0F));
    bgc = palette_rgb(vdp, (uint8_t)(color & 0x0F));

    for (b = 0; b < 8; ++b)
    {
      int dx = col * 8 + b;
      if (dx < (int)d->width)
      {
        px[dx] = ((bits >> (7 - b)) & 1) ? fg : bgc;
      }
    }
  }
}

static void render_multicolor(const VDrip9958* vdp, const Descriptor* d,
                              uint16_t sy, uint32_t* px)
{
  uint32_t nameRow = d->nameBase + (uint32_t)(sy >> 3) * 32u;
  int col;

  for (col = 0; col < 32; ++col)
  {
    /* Each 8-line cell uses two generator bytes (one per 4-line band); the
     * left nibble colors the left four pixels, the right nibble the right. */
    uint8_t name = vdrip9958_vram_read(vdp, nameRow + (uint32_t)col);
    uint8_t data = vdrip9958_vram_read(vdp,
                     d->patternBase + (uint32_t)name * 8u + (uint32_t)((sy >> 2) & 7));
    uint32_t cl = palette_rgb(vdp, (uint8_t)((data >> 4) & 0x0F));
    uint32_t cr = palette_rgb(vdp, (uint8_t)(data & 0x0F));
    int b;
    for (b = 0; b < 8; ++b)
    {
      int dx = col * 8 + b;
      if (dx < (int)d->width) px[dx] = (b < 4) ? cl : cr;
    }
  }
}

static void render_bpp4(const VDrip9958* vdp, const Descriptor* d,
                        uint16_t sy, uint32_t* px)
{
  uint32_t stride = (d->mode == VDRIP9958_MODE_GRAPHIC6) ? 256u : 128u;
  uint32_t lineBase = d->bitmapBase + (uint32_t)sy * stride;
  uint16_t x;
  for (x = 0; x < d->width; ++x)
  {
    uint16_t sx = scroll_source_x(d, x);
    uint8_t byte = vdrip9958_vram_read(vdp, lineBase + (sx >> 1));
    uint8_t code = (sx & 1) ? (uint8_t)(byte & 0x0F) : (uint8_t)((byte >> 4) & 0x0F);
    px[x] = palette_rgb(vdp, code);
  }
}

static void render_bpp2(const VDrip9958* vdp, const Descriptor* d,
                        uint16_t sy, uint32_t* px)
{
  uint32_t lineBase = d->bitmapBase + (uint32_t)sy * 128u;
  uint16_t x;
  for (x = 0; x < d->width; ++x)
  {
    uint16_t sx = scroll_source_x(d, x);
    uint8_t byte = vdrip9958_vram_read(vdp, lineBase + (sx >> 2));
    uint8_t shift = (uint8_t)((3 - (sx & 3)) * 2);
    uint8_t code = (uint8_t)((byte >> shift) & 0x03);
    px[x] = palette_rgb(vdp, code);
  }
}

static void render_direct(const VDrip9958* vdp, const Descriptor* d,
                          uint16_t sy, uint32_t* px)
{
  uint32_t lineBase = d->bitmapBase + (uint32_t)sy * 256u;
  uint16_t x;

  if (!d->yjk)
  {
    for (x = 0; x < d->width; ++x)
    {
      px[x] = g7_direct_rgb(vdrip9958_vram_read(vdp, lineBase + x));
    }
    return;
  }

  /* YJK / YAE: four consecutive pixels share signed J and K. */
  for (x = 0; x < d->width; x += 4)
  {
    uint8_t p[4];
    int j, k, n;
    for (n = 0; n < 4; ++n)
    {
      p[n] = vdrip9958_vram_read(vdp, lineBase + (uint32_t)x + (uint32_t)n);
    }
    /* J = bits of pixels 0/1 low 3 bits; K = pixels 2/3 low 3 bits. */
    j = (int)((p[0] & 0x07) | ((p[1] & 0x07) << 3));
    k = (int)((p[2] & 0x07) | ((p[3] & 0x07) << 3));
    if (j & 0x20) j -= 64;   /* sign-extend 6-bit */
    if (k & 0x20) k -= 64;
    for (n = 0; n < 4; ++n)
    {
      uint16_t dx = (uint16_t)(x + n);
      if (dx >= d->width) break;
      if (d->yae && (p[n] & 0x08))
      {
        /* YAE attribute pixel: palette code in bits 4..7. */
        px[dx] = palette_rgb(vdp, (uint8_t)((p[n] >> 4) & 0x0F));
      }
      else
      {
        int y = (int)((p[n] >> 3) & 0x1F);
        uint8_t R = clamp5(y + j);
        uint8_t G = clamp5(y + k);
        uint8_t B = clamp5((5 * y - 2 * j - k) / 4);
        px[dx] = pack_rgb(expand5(R), expand5(G), expand5(B));
      }
    }
  }
}

/* ==================================================================
 * Sprite evaluation and compositing
 * ================================================================== */

static void render_sprites(const VDrip9958* vdp, const Descriptor* d,
                           uint16_t sy, uint32_t* px, LineStatus* st)
{
  int limit  = (d->spriteMode == 2) ? 8 : 4;
  bool si    = (vdp->registers[1] & 0x02) != 0;   /* 16x16 */
  bool mag   = (vdp->registers[1] & 0x01) != 0;   /* magnify x2 */
  int size   = si ? 16 : 8;
  int sentinel = (d->baseHeight == 212) ? 216 : 208;
  int accepted = 0;
  int horizontalScale =
    (d->mode == VDRIP9958_MODE_GRAPHIC5 ||
     d->mode == VDRIP9958_MODE_GRAPHIC6) ? 2 : 1;
  int s;

  /* Per-pixel sprite occupancy for collision detection. */
  static int8_t occupied[MAX_LINE_PIXELS];
  int i;
  for (i = 0; i < (int)d->width; ++i) occupied[i] = 0;

  for (s = 0; s < MAX_SPRITES; ++s)
  {
    uint32_t attr = d->spriteAttrBase + (uint32_t)s * 4u;
    int spy = (int)vdrip9958_vram_read(vdp, attr + 0);
    int spx;
    uint8_t pattern;
    uint8_t attr3;
    int top, patBase, colByte, color;
    bool ec;
    int line;

    if (spy == sentinel) break;       /* documented end marker */
    top = (spy + 1) & 0xFF;           /* sprite Y is line-minus-one */
    if (top > 256 - 32 && top < 256) top -= 256;
    line = (int)sy - top;
    if (mag) line /= 2;
    if (line < 0 || line >= size) continue;

    if (accepted >= limit)
    {
      st->overflow = true;
      st->overflowSprite = (uint8_t)s;
      break;
    }
    ++accepted;

    spx     = (int)vdrip9958_vram_read(vdp, attr + 1);
    pattern = vdrip9958_vram_read(vdp, attr + 2);
    attr3   = vdrip9958_vram_read(vdp, attr + 3);

    if (d->spriteMode == 2)
    {
      /* Per-line color table sits 512 bytes before the attribute table. */
      uint32_t colBase = d->spriteAttrBase - 512u;
      colByte = vdrip9958_vram_read(vdp, colBase + (uint32_t)s * 16u + (uint32_t)line);
    }
    else
    {
      colByte = attr3;
    }
    color = colByte & 0x0F;
    ec    = (colByte & 0x80) != 0;    /* early clock: shift left 32 */
    spx *= horizontalScale;
    if (ec) spx -= 32 * horizontalScale;

    if (si) patBase = d->spritePatternBase + (uint32_t)(pattern & 0xFC) * 8u;
    else    patBase = d->spritePatternBase + (uint32_t)pattern * 8u;

    /* Render the sprite's pixels on this line. */
    {
      int w;
      for (w = 0; w < size; ++w)
      {
        int patByte = (w < 8) ? 0 : 16;  /* right column for 16x16 */
        int bitcol  = w & 7;
        uint8_t bits = vdrip9958_vram_read(vdp,
                          (uint32_t)patBase + (uint32_t)line + (uint32_t)patByte);
        int set = (bits >> (7 - bitcol)) & 1;
        int reps = (mag ? 2 : 1) * horizontalScale;
        int rep;
        if (!set || color == 0) continue;
        for (rep = 0; rep < reps; ++rep)
        {
          int dx = spx + w * reps + rep;
          if (dx < 0 || dx >= (int)d->width) continue;
          if (occupied[dx] && !st->collision)
          {
            st->collision = true;
            st->collisionX = (uint8_t)dx;
            st->collisionY = (uint8_t)sy;
          }
          occupied[dx] = 1;
          px[dx] = palette_rgb(vdp, (uint8_t)color);
        }
      }
    }
  }
}

/* ==================================================================
 * Status and frame committer
 * ================================================================== */

static void commit_status(VDrip9958* vdp, const Descriptor* d,
                          uint16_t sourceLine, const LineStatus* st)
{
  /* Horizontal-line interrupt: source line matches R#19 and IE1 (R#0 D4). */
  if ((vdp->registers[0] & 0x10) && (sourceLine == vdp->registers[19]))
  {
    vdp->status[1] |= 0x01;  /* S#1 FH */
  }

  if (st->collision)
  {
    vdp->status[0] |= 0x20;  /* S#0 C */
    vdp->status[3] = st->collisionX;
    vdp->status[5] = st->collisionY;
  }
  if (st->overflow)
  {
    vdp->status[0] = (uint8_t)((vdp->status[0] & ~0x1F) | (st->overflowSprite & 0x1F));
    vdp->status[0] |= 0x40;  /* S#0 5S/9S */
  }

  if (st->frameComplete)
  {
    /* Vertical interrupt when enabled (R#1 D5 IE0). */
    if (vdp->registers[1] & 0x20)
    {
      vdp->status[0] |= 0x80;  /* S#0 F */
    }
    /* Advance the next interlace field. */
    vdp->frame.field ^= 1;
    vdp->display.field = vdp->frame.field;
    /* Advance the Text 2 / page blink phase using R#13 ON/OFF nibbles. */
    {
      uint8_t on  = (uint8_t)((vdp->registers[13] >> 4) & 0x0F);
      uint8_t off = (uint8_t)(vdp->registers[13] & 0x0F);
      if (off == 0)
      {
        vdp->frame.blinkPhaseOn = true;
      }
      else if (vdp->frame.blinkPhaseOn)
      {
        if (vdp->frame.blinkOnCount == 0) vdp->frame.blinkOnCount = on;
        if (--vdp->frame.blinkOnCount == 0) vdp->frame.blinkPhaseOn = false;
      }
      else
      {
        if (vdp->frame.blinkOffCount == 0) vdp->frame.blinkOffCount = off;
        if (--vdp->frame.blinkOffCount == 0) vdp->frame.blinkPhaseOn = true;
      }
    }
  }
  (void)d;
}

/* ==================================================================
 * Scanline orchestrator (replaces the Unit 1 placeholder)
 * ================================================================== */

void vdrip9958_render_scanline(VDrip9958* vdp, uint16_t y, uint32_t* pixels)
{
  Descriptor d;
  LineStatus st;
  uint16_t sourceLine;
  uint16_t fieldLine;
  uint16_t x;

  decode_descriptor(vdp, &d);

  /* Out-of-range output line: fill border, no side effects. */
  if (y >= d.outputHeight)
  {
    uint16_t w = (d.width > MAX_LINE_PIXELS) ? MAX_LINE_PIXELS : d.width;
    for (x = 0; x < w; ++x) pixels[x] = d.borderRgb;
    return;
  }

  /* Resolve the source field line (woven interlace: even->field0, odd->field1). */
  if (d.interlaced)
  {
    fieldLine = (uint16_t)(y >> 1);
  }
  else
  {
    fieldLine = y;
  }
  /* Vertical display start / scroll (R#23). */
  sourceLine = (uint16_t)((fieldLine + vdp->registers[23]) & 0xFF);

  /* Initialize the whole active width to border/background color. */
  for (x = 0; x < d.width; ++x) pixels[x] = d.borderRgb;

  st.collision = false; st.collisionX = 0; st.collisionY = 0;
  st.overflow = false;  st.overflowSprite = 0;
  st.horizontalInt = false; st.verticalInt = false;
  st.frameComplete = (y == (uint16_t)(d.outputHeight - 1));

  if (!d.blanked && d.format != PF_BORDER)
  {
    switch (d.format)
    {
      case PF_TEXT:       render_text(vdp, &d, sourceLine, pixels);       break;
      case PF_PATTERN:    render_pattern(vdp, &d, sourceLine, pixels);    break;
      case PF_MULTICOLOR: render_multicolor(vdp, &d, sourceLine, pixels); break;
      case PF_BPP4:       render_bpp4(vdp, &d, sourceLine, pixels);       break;
      case PF_BPP2:       render_bpp2(vdp, &d, sourceLine, pixels);       break;
      case PF_DIRECT:     render_direct(vdp, &d, sourceLine, pixels);     break;
      case PF_BORDER:     break;
    }

    /* MSK: replace the masked left edge with border color. */
    for (x = 0; x < d.maskPixels && x < d.width; ++x) pixels[x] = d.borderRgb;

    if (d.spriteMode != 0)
    {
      render_sprites(vdp, &d, sourceLine, pixels, &st);
    }
  }

  commit_status(vdp, &d, sourceLine, &st);
}
