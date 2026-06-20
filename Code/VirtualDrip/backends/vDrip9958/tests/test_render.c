/*
 * vDrip9958 - Unit 2 rendering smoke test.
 *
 * Representative coverage of the rendering path through the public API:
 * bitmap palette conversion, Graphic 7 direct RGB, YJK, mode dimensions,
 * woven interlace metadata, sprite modes 1 and 2, repeated-line status
 * accumulation, and final-line frame completion. Structural/observable
 * assertions only (the project intentionally has no golden-image suite).
 */

#include "vDrip9958.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

/* Color helpers mirroring the renderer's integer conversions. */
static uint32_t e3(uint8_t c) { return ((uint32_t)(c & 7) * 255u + 3u) / 7u; }
static uint32_t e2(uint8_t c) { return ((uint32_t)(c & 3) * 255u + 1u) / 3u; }
static uint32_t pal(uint8_t r, uint8_t g, uint8_t b)
{
  return (e3(r) << 16) | (e3(g) << 8) | e3(b);
}

static void write_reg(VDrip9958* v, uint8_t reg, uint8_t val)
{
  vDrip9958WriteControl(v, val);
  vDrip9958WriteControl(v, (uint8_t)(0x80 | reg));
}

static void vram_write(VDrip9958* v, uint16_t addr, uint8_t val)
{
  vDrip9958WriteControl(v, (uint8_t)(addr & 0xFF));
  vDrip9958WriteControl(v, (uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
  vDrip9958WriteData(v, val);
}

/* Enable display (BL) with the given mode register values. */
static void set_mode(VDrip9958* v, uint8_t r0, uint8_t r1_extra)
{
  write_reg(v, 0, r0);
  write_reg(v, 1, (uint8_t)(0x40 | r1_extra)); /* BL on */
}

static void test_graphic4_palette(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];
  VDrip9958DisplayInfo info;

  set_mode(v, 0x06, 0x00);          /* Graphic 4 */
  write_reg(v, 2, 0x00);            /* bitmap base 0 */
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.mode == VDRIP9958_MODE_GRAPHIC4);
  CHECK(info.width == 256);

  vram_write(v, 0x0000, 0x12);      /* pixel0 = code 1, pixel1 = code 2 */

  vDrip9958ScanLine(v, 0, line);
  CHECK(line[0] == pal(0, 0, 0));   /* MSX2 palette index 1 = black */
  CHECK(line[1] == pal(1, 6, 1));   /* MSX2 palette index 2 */

  vDrip9958Destroy(v);
}

static void test_graphic7_direct(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];

  set_mode(v, 0x0E, 0x00);          /* Graphic 7 */
  write_reg(v, 2, 0x00);
  write_reg(v, 25, 0x00);           /* YJK/YAE off */

  vram_write(v, 0x0000, 0xFF);      /* GRB332 all max -> white */
  vram_write(v, 0x0001, 0x80);      /* G=4, R=0, B=0 */

  vDrip9958ScanLine(v, 0, line);
  CHECK(line[0] == ((e3(7) << 16) | (e3(7) << 8) | e2(3)));
  CHECK(line[1] == ((e3(0) << 16) | (e3(4) << 8) | e2(0)));

  vDrip9958Destroy(v);
}

static void test_graphic7_yjk_gray(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];
  uint32_t p;

  set_mode(v, 0x0E, 0x00);          /* Graphic 7 */
  write_reg(v, 2, 0x00);
  write_reg(v, 25, 0x08);           /* YJK on, YAE off */

  /* Four pixels with J=K=0 (low 3 bits zero) and a mid Y -> neutral gray. */
  vram_write(v, 0x0000, (uint8_t)(10 << 3));
  vram_write(v, 0x0001, (uint8_t)(10 << 3));
  vram_write(v, 0x0002, (uint8_t)(10 << 3));
  vram_write(v, 0x0003, (uint8_t)(10 << 3));

  vDrip9958ScanLine(v, 0, line);
  p = line[0];
  /* With J=K=0, R=G=Y and B≈Y: red and green channels are equal (gray). */
  CHECK(((p >> 16) & 0xFF) == ((p >> 8) & 0xFF));

  vDrip9958Destroy(v);
}

static void test_mode_widths(void)
{
  VDrip9958* v = vDrip9958New();
  VDrip9958DisplayInfo info;

  set_mode(v, 0x08, 0x00);          /* Graphic 5 */
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.mode == VDRIP9958_MODE_GRAPHIC5);
  CHECK(info.width == 512);

  set_mode(v, 0x0A, 0x00);          /* Graphic 6 */
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.mode == VDRIP9958_MODE_GRAPHIC6);
  CHECK(info.width == 512);

  set_mode(v, 0x00, 0x00);          /* Graphic 1 */
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.width == 256);

  vDrip9958Destroy(v);
}

static void test_interlace_metadata(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];
  VDrip9958DisplayInfo info;

  set_mode(v, 0x06, 0x00);          /* Graphic 4 */
  write_reg(v, 9, 0x08);            /* IL: interlace, base 192 */
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.interlaced == true);
  CHECK(info.height == 384);        /* woven output height */

  /* A line beyond the woven height is a border fill with no crash. */
  vDrip9958ScanLine(v, 384, line);
  CHECK(line[0] == pal(0, 0, 0));   /* backdrop index 0 */

  vDrip9958Destroy(v);
}

/* Build a sprite pattern of all-set rows at the given base. */
static void write_full_sprite_pattern(VDrip9958* v, uint16_t base)
{
  int i;
  for (i = 0; i < 8; ++i) vram_write(v, (uint16_t)(base + i), 0xFF);
}

static void test_sprite_mode1(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];

  set_mode(v, 0x00, 0x00);          /* Graphic 1 -> sprite mode 1 */
  write_reg(v, 5, 0x20);            /* sprite attr base 0x1000 */
  write_reg(v, 6, 0x04);            /* sprite pattern base 0x2000 */

  write_full_sprite_pattern(v, 0x2000);
  /* One sprite: Y=0 (visible at line 1), X=0, pattern 0, color 15. */
  vram_write(v, 0x1000, 0x00);
  vram_write(v, 0x1001, 0x00);
  vram_write(v, 0x1002, 0x00);
  vram_write(v, 0x1003, 0x0F);
  vram_write(v, 0x1004, 0xD0);      /* sentinel (208) */

  vDrip9958ScanLine(v, 1, line);
  CHECK(line[0] == pal(7, 7, 7));   /* sprite color 15 = white */
  CHECK(line[7] == pal(7, 7, 7));

  vDrip9958Destroy(v);
}

static void test_sprite_mode2_and_collision(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];
  uint8_t s0;

  set_mode(v, 0x08, 0x00);          /* Graphic 3 -> sprite mode 2 */
  write_reg(v, 5, 0x20);            /* attr base 0x1000; color base 0x0E00 */
  write_reg(v, 6, 0x04);            /* pattern base 0x2000 */

  write_full_sprite_pattern(v, 0x2000);

  /* Two overlapping sprites at X=0,Y=0 -> collision. Mode 2 per-line color. */
  vram_write(v, 0x0E00, 0x0F);      /* sprite 0, line 0 color 15 */
  vram_write(v, 0x0E10, 0x0F);      /* sprite 1, line 0 color 15 */
  vram_write(v, 0x1000, 0x00); vram_write(v, 0x1001, 0x00); vram_write(v, 0x1002, 0x00);
  vram_write(v, 0x1004, 0x00); vram_write(v, 0x1005, 0x00); vram_write(v, 0x1006, 0x00);
  vram_write(v, 0x1008, 0xD8);      /* sentinel (216, 212-line uses 216 but 192 uses 208) */
  vram_write(v, 0x1008, 0xD0);

  vDrip9958ScanLine(v, 1, line);
  CHECK(line[0] == pal(7, 7, 7));   /* sprite color 15 */

  write_reg(v, 15, 0);              /* select S#0 */
  s0 = vDrip9958ReadStatus(v);
  CHECK((s0 & 0x20) != 0);          /* collision flag */

  vDrip9958Destroy(v);
}

static void test_frame_completion(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];
  VDrip9958DisplayInfo before, after;
  uint8_t s0;

  set_mode(v, 0x00, 0x20);          /* Graphic 1, IE0 (vertical int) enabled */
  before = vDrip9958GetDisplayInfo(v);

  /* Final line of a 192-line progressive frame is line 191. */
  vDrip9958ScanLine(v, 191, line);

  write_reg(v, 15, 0);
  s0 = vDrip9958ReadStatus(v);
  CHECK((s0 & 0x80) != 0);          /* S#0 F vertical interrupt set */

  after = vDrip9958GetDisplayInfo(v);
  CHECK(after.field != before.field); /* field advanced at frame completion */

  vDrip9958Destroy(v);
}

static void test_repeated_line_accumulates(void)
{
  VDrip9958* v = vDrip9958New();
  uint32_t line[VDRIP9958_MAX_WIDTH];

  set_mode(v, 0x00, 0x20);          /* Graphic 1, IE0 enabled */

  /* Rendering the final line twice completes two frames; status persists
   * until read. Both renders leave the F flag set. */
  vDrip9958ScanLine(v, 191, line);
  vDrip9958ScanLine(v, 191, line);

  write_reg(v, 15, 0);
  CHECK((vDrip9958ReadStatus(v) & 0x80) != 0);

  vDrip9958Destroy(v);
}

int main(void)
{
  test_graphic4_palette();
  test_graphic7_direct();
  test_graphic7_yjk_gray();
  test_mode_widths();
  test_interlace_metadata();
  test_sprite_mode1();
  test_sprite_mode2_and_collision();
  test_frame_completion();
  test_repeated_line_accumulates();

  if (failures == 0)
  {
    printf("test_render: all checks passed\n");
    return 0;
  }
  printf("test_render: %d check(s) failed\n", failures);
  return 1;
}
