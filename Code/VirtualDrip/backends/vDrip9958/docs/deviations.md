# Deviations and Known Gaps

vDrip9958 emulates the software-visible digital behavior of the V9958. This
document lists intentional exclusions and deterministic choices, and the
behavior that is implemented but only manually reviewed.

## Intentionally unsupported

- **External video / superimpose / color bus** — out of scope; related register
  bits (e.g. R#0 EV, R#8 CB) are stored as zero and have no effect.
- **Light pen and mouse** — deleted on the V9958; related R#8 bits and status
  fields remain zero.
- **WAIT / analog functions** — not modeled.
- **Cycle-accurate timing** — vDrip9958 uses *functional* timing. Command
  progress and CE/TR are observable through explicit `vDrip9958StepCommand()`
  calls and scanline rendering, not VDP cycles. There is no internal frame
  clock; rendering the final output line completes a frame.

## Hardware configuration choices

- **Fixed 128 KiB display VRAM.** No expansion VRAM exists: a selected expansion
  source reads zero and a selected expansion destination discards writes, while
  coordinates, counts, and CE/TR progression stay deterministic.
- **Undefined reset state is zero.** Any value the manuals leave undefined is
  initialized to zero, including all VRAM.

## Implemented but manually reviewed (not smoke-tested)

These follow the documented model and build/run cleanly, but have no dedicated
automated case and warrant manual validation against the manuals:

- Bitmap page-base formulas from R#2 (Graphic 4–7).
- Graphic 2/3 three-bank pattern/color selection.
- Sprite 16×16 quadrant byte ordering, the sprite mode-2 color-table −512
  offset, and Graphic 5 even/odd sprite tiling.
- Horizontal scroll exact edge behavior; **two-page (SP2) wrapping is not yet
  implemented** (single-page wrapping only).
- **R#18 display adjustment is not yet applied** to the active region (R#23
  vertical start is applied).
- Command overlap exactness for HMMM/LMMM/YMMM across DIX/DIY; YMMM edge
  extension.
- LINE error-term initial value / tie-breaking versus hardware.
- The post-command register table is **partial** (final DY and remaining NY are
  written; other result fields are not fully populated).
- ARG (R#45) bit assignments (MAJ/EQ/DIX/DIY/MXS/MXD) and the SRCH boundary-X
  result width.

## Deterministic interpretation choices

- 3-bit and 2-bit color components expand with rounding
  (`(c·255+3)/7`, `(c·255+1)/3`); 5-bit YJK components use `(c·255+15)/31`.
- YJK blue uses `(5·Y − 2·J − K)/4` with C integer division toward zero, clamped
  to 0..31.
- Reserved logical-operation codes leave the destination unchanged.
- Invalid/reserved display modes report `VDRIP9958_MODE_INVALID` and render
  deterministic border output while retaining the last valid geometry.
