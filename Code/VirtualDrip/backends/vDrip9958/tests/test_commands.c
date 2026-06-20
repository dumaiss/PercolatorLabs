/*
 * vDrip9958 - Unit 3 command-engine smoke test.
 *
 * Representative coverage of the command engine through the public API:
 * start/step/completion/STOP, HMMC CPU input + TR, LMCM S#7/TR (including the
 * final unread output), copy/fill, a transparent logical operation, LINE,
 * SRCH (found and boundary), PSET, POINT, and a CMD-expanded command outside
 * the bitmap modes. Explicit stepping only; no time-based waiting.
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

/* Command code nibbles. */
#define C_STOP  0x0
#define C_POINT 0x4
#define C_PSET  0x5
#define C_SRCH  0x6
#define C_LINE  0x7
#define C_LMMV  0x8
#define C_LMCM  0xA
#define C_LMMC  0xB
#define C_HMMV  0xC
#define C_HMMC  0xF

static void write_reg(VDrip9958* v, uint8_t reg, uint8_t val)
{
  vDrip9958WriteControl(v, val);
  vDrip9958WriteControl(v, (uint8_t)(0x80 | reg));
}

static uint8_t read_vram(VDrip9958* v, uint16_t addr)
{
  vDrip9958WriteControl(v, (uint8_t)(addr & 0xFF));
  vDrip9958WriteControl(v, (uint8_t)((addr >> 8) & 0x3F));
  return vDrip9958ReadData(v);
}

static void write_vram(VDrip9958* v, uint16_t addr, uint8_t val)
{
  vDrip9958WriteControl(v, (uint8_t)(addr & 0xFF));
  vDrip9958WriteControl(v, (uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
  vDrip9958WriteData(v, val);
}

static uint8_t read_status(VDrip9958* v, uint8_t sel)
{
  write_reg(v, 15, sel);
  return vDrip9958ReadStatus(v);
}

/* Enter Graphic 4 with display enabled. */
static void g4_mode(VDrip9958* v)
{
  write_reg(v, 0, 0x06);
  write_reg(v, 1, 0x40);
  write_reg(v, 2, 0x00);
}

static void set_cmd(VDrip9958* v, uint16_t sx, uint16_t sy, uint16_t dx,
                    uint16_t dy, uint16_t nx, uint16_t ny, uint8_t color,
                    uint8_t arg)
{
  write_reg(v, 32, (uint8_t)(sx & 0xFF)); write_reg(v, 33, (uint8_t)((sx >> 8) & 1));
  write_reg(v, 34, (uint8_t)(sy & 0xFF)); write_reg(v, 35, (uint8_t)((sy >> 8) & 3));
  write_reg(v, 36, (uint8_t)(dx & 0xFF)); write_reg(v, 37, (uint8_t)((dx >> 8) & 1));
  write_reg(v, 38, (uint8_t)(dy & 0xFF)); write_reg(v, 39, (uint8_t)((dy >> 8) & 3));
  write_reg(v, 40, (uint8_t)(nx & 0xFF)); write_reg(v, 41, (uint8_t)((nx >> 8) & 1));
  write_reg(v, 42, (uint8_t)(ny & 0xFF)); write_reg(v, 43, (uint8_t)((ny >> 8) & 3));
  write_reg(v, 44, color);
  write_reg(v, 45, arg);
}

static void trigger(VDrip9958* v, uint8_t code, uint8_t logop)
{
  write_reg(v, 46, (uint8_t)((code << 4) | (logop & 0x0F)));
}

static void run_to_idle(VDrip9958* v)
{
  int guard = 0;
  while (vDrip9958StepCommand(v) && guard++ < 100000) { }
}

static uint8_t pixel_g4(VDrip9958* v, uint16_t x, uint16_t y)
{
  uint8_t b = read_vram(v, (uint16_t)(y * 128 + x / 2));
  return (x & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)((b >> 4) & 0x0F);
}

static void test_hmmv_fill_and_ce(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  set_cmd(v, 0, 0, 0, 0, 4, 1, 0xFF, 0x00);
  trigger(v, C_HMMV, 0);

  CHECK((read_status(v, 2) & 0x01) != 0);   /* CE set after start */
  run_to_idle(v);
  CHECK(read_vram(v, 0x0000) == 0xFF);
  CHECK(read_vram(v, 0x0001) == 0xFF);
  CHECK((read_status(v, 2) & 0x01) == 0);   /* CE clear after completion */
  CHECK(vDrip9958StepCommand(v) == false);

  vDrip9958Destroy(v);
}

static void test_stop(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  set_cmd(v, 0, 0, 0, 0, 256, 200, 0xFF, 0x00); /* big fill */
  trigger(v, C_HMMV, 0);
  vDrip9958StepCommand(v);                   /* one byte written */
  CHECK(read_vram(v, 0x0000) == 0xFF);

  trigger(v, C_STOP, 0);                      /* abort */
  CHECK((read_status(v, 2) & 0x01) == 0);     /* CE clear */
  CHECK(vDrip9958StepCommand(v) == false);
  CHECK(read_vram(v, 0x0002) == 0x00);        /* later bytes untouched */

  vDrip9958Destroy(v);
}

static void test_hmmc_cpu_input(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  set_cmd(v, 0, 0, 0, 0, 4, 1, 0xAA, 0x00);   /* first byte = 0xAA */
  trigger(v, C_HMMC, 0);

  vDrip9958StepCommand(v);                     /* writes 0xAA, then waits */
  CHECK(read_vram(v, 0x0000) == 0xAA);
  CHECK((read_status(v, 2) & 0x80) != 0);      /* TR set, awaiting input */

  write_reg(v, 44, 0xBB);                      /* supply next byte */
  CHECK((read_status(v, 2) & 0x80) == 0);      /* TR cleared */
  run_to_idle(v);
  CHECK(read_vram(v, 0x0001) == 0xBB);

  vDrip9958Destroy(v);
}

static void test_lmmv_logical_pixels(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  set_cmd(v, 0, 0, 0, 0, 2, 1, 0x05, 0x00);   /* color 5 */
  trigger(v, C_LMMV, 0);                       /* IMP */
  run_to_idle(v);
  CHECK(pixel_g4(v, 0, 0) == 5);
  CHECK(pixel_g4(v, 1, 0) == 5);

  vDrip9958Destroy(v);
}

static void test_transparent_op(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  /* Seed pixel (0,0) = 5 with PSET. */
  set_cmd(v, 0, 0, 0, 0, 1, 1, 0x05, 0x00);
  trigger(v, C_PSET, 0);
  run_to_idle(v);
  CHECK(pixel_g4(v, 0, 0) == 5);

  /* Transparent IMP with source 0 must leave the destination unchanged. */
  set_cmd(v, 0, 0, 0, 0, 1, 1, 0x00, 0x00);
  trigger(v, C_LMMV, 0x08);                    /* TIMP */
  run_to_idle(v);
  CHECK(pixel_g4(v, 0, 0) == 5);

  vDrip9958Destroy(v);
}

static void test_lmcm_output(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  write_vram(v, 0x0000, 0x12);                 /* pixel0=1, pixel1=2 */
  set_cmd(v, 0, 0, 0, 0, 2, 1, 0x00, 0x00);
  trigger(v, C_LMCM, 0);

  vDrip9958StepCommand(v);                      /* publish pixel 0 */
  CHECK((read_status(v, 2) & 0x80) != 0);       /* TR set */
  CHECK(read_status(v, 7) == 1);                /* S#7 = pixel0; read clears TR */

  vDrip9958StepCommand(v);                      /* publish pixel 1 (final) */
  CHECK(read_status(v, 7) == 2);                /* S#7 = pixel1 */
  CHECK(vDrip9958StepCommand(v) == false);      /* command complete */

  vDrip9958Destroy(v);
}

static void test_line(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);
  /* Horizontal line of color 5 from (0,0), major X length 4, minor 0. */
  set_cmd(v, 0, 0, 0, 0, 4, 0, 0x05, 0x00);
  trigger(v, C_LINE, 0);
  run_to_idle(v);
  CHECK(pixel_g4(v, 0, 0) == 5);
  CHECK(pixel_g4(v, 2, 0) == 5);
  CHECK(pixel_g4(v, 4, 0) == 5);

  vDrip9958Destroy(v);
}

static void test_srch(void)
{
  VDrip9958* v = vDrip9958New();
  uint8_t s2;
  g4_mode(v);
  write_vram(v, 0x0001, 0x07);                  /* pixel x=3 -> color 7 */

  /* Find color equal to 7, scanning right from x=0. */
  set_cmd(v, 0, 0, 0, 0, 0, 0, 0x07, 0x02);     /* arg EQ=1 */
  trigger(v, C_SRCH, 0);
  run_to_idle(v);
  s2 = read_status(v, 2);
  CHECK((s2 & 0x10) != 0);                       /* BD: border found */
  CHECK(read_status(v, 8) == 3);                 /* S#8 = X of match */

  vDrip9958Destroy(v);
}

static void test_pset_point(void)
{
  VDrip9958* v = vDrip9958New();
  g4_mode(v);

  set_cmd(v, 0, 0, 1, 0, 1, 1, 0x0F, 0x00);
  trigger(v, C_PSET, 0);
  run_to_idle(v);
  CHECK(pixel_g4(v, 1, 0) == 0x0F);

  /* POINT reads (0,0) into S#7. Seed it first. */
  write_vram(v, 0x0000, 0x90);                   /* pixel0 = 9 */
  set_cmd(v, 0, 0, 0, 0, 1, 1, 0x00, 0x00);
  trigger(v, C_POINT, 0);
  run_to_idle(v);
  CHECK(read_status(v, 7) == 9);

  vDrip9958Destroy(v);
}

static void test_cmd_expanded_outside_bitmap(void)
{
  VDrip9958* v = vDrip9958New();
  /* Graphic 1 (not a bitmap command mode). */
  write_reg(v, 0, 0x00);
  write_reg(v, 1, 0x40);

  /* Without CMD: a command in G1 is unsupported and inert (no CE). */
  write_reg(v, 25, 0x00);
  set_cmd(v, 0, 0, 0, 0, 1, 1, 0x77, 0x00);
  trigger(v, C_HMMV, 0);
  CHECK((read_status(v, 2) & 0x01) == 0);        /* never became active */

  /* With CMD set: commands run in Graphic 7 (8-bit) interpretation. */
  write_reg(v, 25, 0x40);
  set_cmd(v, 0, 0, 0, 0, 1, 1, 0x77, 0x00);
  trigger(v, C_HMMV, 0);
  run_to_idle(v);
  CHECK(read_vram(v, 0x0000) == 0x77);           /* one byte per pixel */

  vDrip9958Destroy(v);
}

int main(void)
{
  test_hmmv_fill_and_ce();
  test_stop();
  test_hmmc_cpu_input();
  test_lmmv_logical_pixels();
  test_transparent_op();
  test_lmcm_output();
  test_line();
  test_srch();
  test_pset_point();
  test_cmd_expanded_outside_bitmap();

  if (failures == 0)
  {
    printf("test_commands: all checks passed\n");
    return 0;
  }
  printf("test_commands: %d check(s) failed\n", failures);
  return 1;
}
