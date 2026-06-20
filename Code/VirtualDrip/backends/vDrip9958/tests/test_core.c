/*
 * vDrip9958 - Unit 1 public-API core smoke test.
 *
 * Exercises only the public header: lifecycle, null safety, VRAM read-ahead,
 * register/palette/status/display behavior through the CPU ports, and the
 * inert Unit 2/3 placeholders. No external test framework: the program prints
 * each failure and returns non-zero if any check fails.
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

/* 3-bit -> 8-bit expansion, matching the core's placeholder color path. */
#define EXP(c) ((uint32_t)(c) * 255u / 7u)
#define RGB3(r, g, b) ((EXP(r) << 16) | (EXP(g) << 8) | EXP(b))

/* Port helpers (mirror the documented two-write control protocol). */
static void write_reg(VDrip9958* v, uint8_t reg, uint8_t val)
{
  vDrip9958WriteControl(v, val);
  vDrip9958WriteControl(v, (uint8_t)(0x80 | reg));
}

static void set_addr_write(VDrip9958* v, uint16_t addr)
{
  vDrip9958WriteControl(v, (uint8_t)(addr & 0xFF));
  vDrip9958WriteControl(v, (uint8_t)(0x40 | ((addr >> 8) & 0x3F)));
}

static void set_addr_read(VDrip9958* v, uint16_t addr)
{
  vDrip9958WriteControl(v, (uint8_t)(addr & 0xFF));
  vDrip9958WriteControl(v, (uint8_t)((addr >> 8) & 0x3F));
}

static void test_null_safety(void)
{
  VDrip9958DisplayInfo info;

  vDrip9958Reset(NULL);
  vDrip9958Destroy(NULL);
  vDrip9958WriteData(NULL, 0x12);
  vDrip9958WriteControl(NULL, 0x34);
  vDrip9958WritePalette(NULL, 0x56);
  vDrip9958WriteRegisterIndirect(NULL, 0x78);
  vDrip9958ScanLine(NULL, 0, NULL);

  CHECK(vDrip9958ReadData(NULL) == 0);
  CHECK(vDrip9958ReadStatus(NULL) == 0);
  CHECK(vDrip9958StepCommand(NULL) == false);

  info = vDrip9958GetDisplayInfo(NULL);
  CHECK(info.mode == VDRIP9958_MODE_INVALID);
}

static void test_vram_readahead(VDrip9958* v)
{
  set_addr_write(v, 0x0000);
  vDrip9958WriteData(v, 0xAB);
  vDrip9958WriteData(v, 0xCD);
  vDrip9958WriteData(v, 0xEF);

  /* A read setup preloads the byte at the address and advances; the first
   * data read returns that preloaded byte. */
  set_addr_read(v, 0x0000);
  CHECK(vDrip9958ReadData(v) == 0xAB);
  CHECK(vDrip9958ReadData(v) == 0xCD);
  CHECK(vDrip9958ReadData(v) == 0xEF);
}

static void test_instance_independence(void)
{
  VDrip9958* a = vDrip9958New();
  VDrip9958* b = vDrip9958New();
  CHECK(a != NULL);
  CHECK(b != NULL);

  set_addr_write(a, 0x0010);
  vDrip9958WriteData(a, 0x55);

  set_addr_read(b, 0x0010);
  CHECK(vDrip9958ReadData(b) == 0x00); /* b is untouched */

  vDrip9958Destroy(a);
  vDrip9958Destroy(b);
}

static void test_reset_baseline(VDrip9958* v)
{
  VDrip9958DisplayInfo info;

  /* Zeroed registers decode to a valid GRAPHIC1 256x192 baseline. */
  vDrip9958Reset(v);
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.mode == VDRIP9958_MODE_GRAPHIC1);
  CHECK(info.width == 256);
  CHECK(info.height == 192);
  CHECK(info.interlaced == false);
}

static void test_invalid_mode_retains_geometry(VDrip9958* v)
{
  VDrip9958DisplayInfo info;

  vDrip9958Reset(v);
  /* M1+M2 together (R#1 D4|D3) with M4 set is a reserved combination. */
  write_reg(v, 0, 0x04); /* M4 */
  write_reg(v, 1, 0x18); /* M1 | M2 */
  info = vDrip9958GetDisplayInfo(v);
  CHECK(info.mode == VDRIP9958_MODE_INVALID);
  CHECK(info.width == 256);   /* retained from the last valid mode */
  CHECK(info.height == 192);
}

static void test_palette_and_backdrop(VDrip9958* v)
{
  uint32_t line[VDRIP9958_MAX_WIDTH];

  vDrip9958Reset(v);

  /* Program palette entry 3 to (r=7,g=0,b=0) via the palette port. */
  write_reg(v, 16, 3);              /* select palette index 3   */
  vDrip9958WritePalette(v, 0x70);   /* first byte: R=7, B=0      */
  vDrip9958WritePalette(v, 0x00);   /* second byte: G=0          */

  /* Backdrop = index 3; the placeholder fills the line with it. */
  write_reg(v, 7, 0x03);
  vDrip9958ScanLine(v, 0, line);
  CHECK(line[0] == RGB3(7, 0, 0));
  CHECK(line[255] == RGB3(7, 0, 0));

  /* R#16 auto-advanced past the committed entry. */
  /* (Observed indirectly: a second commit lands on index 4.) */
}

static void test_status_select(VDrip9958* v)
{
  vDrip9958Reset(v);

  write_reg(v, 15, 1);                       /* select S#1 */
  CHECK(vDrip9958ReadStatus(v) == 0x04);     /* V9958 identification */

  write_reg(v, 15, 15);                      /* unsupported selection */
  CHECK(vDrip9958ReadStatus(v) == 0x00);
}

static void test_indirect_matches_direct(void)
{
  VDrip9958* a = vDrip9958New();
  VDrip9958* b = vDrip9958New();
  uint32_t la[VDRIP9958_MAX_WIDTH];
  uint32_t lb[VDRIP9958_MAX_WIDTH];

  /* Direct write of backdrop R#7 = 5. */
  write_reg(a, 7, 0x05);

  /* Indirect write of the same register via R#17 = 7 (no auto-inc inhibit). */
  write_reg(b, 17, 0x07);
  vDrip9958WriteRegisterIndirect(b, 0x05);

  vDrip9958ScanLine(a, 0, la);
  vDrip9958ScanLine(b, 0, lb);
  CHECK(la[0] == lb[0]);

  vDrip9958Destroy(a);
  vDrip9958Destroy(b);
}

static void test_command_seam_inert(VDrip9958* v)
{
  vDrip9958Reset(v);
  CHECK(vDrip9958StepCommand(v) == false);

  /* Writing the command trigger register must not start anything in Unit 1. */
  write_reg(v, 46, 0xF0);
  CHECK(vDrip9958StepCommand(v) == false);
}

int main(void)
{
  VDrip9958* v = vDrip9958New();
  CHECK(v != NULL);
  if (v == NULL)
  {
    return 1;
  }

  test_null_safety();
  test_vram_readahead(v);
  test_instance_independence();
  test_reset_baseline(v);
  test_invalid_mode_retains_geometry(v);
  test_palette_and_backdrop(v);
  test_status_select(v);
  test_indirect_matches_direct();
  test_command_seam_inert(v);

  vDrip9958Destroy(v);

  if (failures == 0)
  {
    printf("test_core: all checks passed\n");
    return 0;
  }
  printf("test_core: %d check(s) failed\n", failures);
  return 1;
}
