/*
 * vDrip9958 - Unit 1 white-box core smoke test.
 *
 * Includes the private header to verify invariants that are not observable
 * through the public API: separate state/VRAM ownership, register writable
 * masks, 17-bit boundary addressing, and the deterministic reset state.
 */

#include "vDrip9958_internal.h"

#include <stdio.h>

static int failures = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

static void write_reg(VDrip9958* v, uint8_t reg, uint8_t val)
{
  vDrip9958WriteControl(v, val);
  vDrip9958WriteControl(v, (uint8_t)(0x80 | reg));
}

static void test_separate_ownership(void)
{
  VDrip9958* a = vDrip9958New();
  VDrip9958* b = vDrip9958New();

  CHECK(a != NULL && b != NULL);
  CHECK(a->vram != NULL && b->vram != NULL);
  CHECK(a->vram != b->vram);     /* distinct VRAM blocks */
  CHECK(a->vram != (uint8_t*)a); /* VRAM is a separate allocation */

  vDrip9958Destroy(a);
  vDrip9958Destroy(b);
}

static void test_register_masks(void)
{
  VDrip9958* v = vDrip9958New();

  write_reg(v, 0, 0xFF);  CHECK(v->registers[0] == 0xFE); /* EV deleted    */
  write_reg(v, 1, 0xFF);  CHECK(v->registers[1] == 0x7F); /* D7 reserved   */
  write_reg(v, 8, 0xFF);  CHECK(v->registers[8] == 0x2F); /* MS/LP/CB gone */
  write_reg(v, 17, 0xFF); CHECK(v->registers[17] == 0xBF);/* D6 reserved   */
  write_reg(v, 2, 0xFF);  CHECK(v->registers[2] == 0xFF); /* full byte     */

  /* Unsupported register numbers are ignored entirely. */
  write_reg(v, 30, 0xFF); CHECK(v->registers[30] == 0x00);
  write_reg(v, 50, 0xFF); CHECK(v->registers[50] == 0x00);

  vDrip9958Destroy(v);
}

static void test_boundary_address(void)
{
  VDrip9958* v = vDrip9958New();

  /* Construct the top-of-VRAM address: A16..A14 from R#14, A13..A0 from the
   * two control bytes. */
  write_reg(v, 14, 0x07);
  vDrip9958WriteControl(v, 0xFF);                 /* A7..A0           */
  vDrip9958WriteControl(v, (uint8_t)(0x40 | 0x3F)); /* write, A13..A8 */
  CHECK(v->vramAddress == 0x1FFFF);

  vDrip9958WriteData(v, 0x99);
  CHECK(v->vram[0x1FFFF] == 0x99);

  /* The increment wraps within the 17-bit space and carries R#14 to 0. */
  CHECK(v->vramAddress == 0x00000);
  CHECK(v->registers[14] == 0x00);

  vDrip9958Destroy(v);
}

static void test_reset_state(void)
{
  VDrip9958* v = vDrip9958New();
  int i;
  int all_regs_zero = 1;

  vDrip9958Reset(v);

  for (i = 0; i < VDRIP9958_NUM_REGISTERS; ++i)
  {
    if (v->registers[i] != 0) { all_regs_zero = 0; }
  }
  CHECK(all_regs_zero);

  CHECK(v->status[1] == VDRIP9958_ID_S1);
  CHECK(v->palette[2].r == 1 && v->palette[2].g == 6 && v->palette[2].b == 1);
  CHECK(v->palette[15].r == 7 && v->palette[15].g == 7 && v->palette[15].b == 7);
  CHECK(v->vramAddress == 0x00000);
  CHECK(v->controlLatch.full == false);
  CHECK(v->paletteLatch.full == false);
  CHECK(v->command.active == false);
  CHECK(v->command.transferReady == false);
  CHECK(v->vram[0] == 0 && v->vram[VDRIP9958_VRAM_SIZE - 1] == 0);

  vDrip9958Destroy(v);
}

int main(void)
{
  test_separate_ownership();
  test_register_masks();
  test_boundary_address();
  test_reset_state();

  if (failures == 0)
  {
    printf("test_core_internal: all checks passed\n");
    return 0;
  }
  printf("test_core_internal: %d check(s) failed\n", failures);
  return 1;
}
