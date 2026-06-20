# Register Support

Status legend: **ST** = implemented + smoke-tested, **MR** = implemented +
manually reviewed (no dedicated automated case), **U** = unsupported (out of
scope). Reset value is zero for all control registers unless noted. Behavior
follows the Yamaha V9938/V9958 manuals (`docs/yamaha_v9938.pdf`,
`docs/yamaha_v9958_ocr.pdf`).

## Control registers

| Reg | Name | Implemented behavior | Status | Evidence |
|---|---|---|---|---|
| R#0 | Mode/IE1 | Mode bits M3–M5, IE1; D0 (EV) deleted (mask 0xFE) | MR | `decode_descriptor`, mask table |
| R#1 | Mode/BL/IE0 | Mode bits M1–M2, blank (BL), IE0, sprite size/mag; D7 reserved (mask 0x7F) | ST | `test_render`, `test_core_internal` |
| R#2 | Pattern name / bitmap page base | Name table / bitmap page base | MR | renderer/command base decode |
| R#3,R#10 | Color table base | Color table base (extended) | MR | pattern renderers |
| R#4 | Pattern generator base | Pattern generator base | MR | pattern renderers |
| R#5,R#11 | Sprite attribute base | Sprite attribute table base | MR | sprite evaluator |
| R#6 | Sprite pattern base | Sprite pattern generator base | MR | sprite evaluator |
| R#7 | Border / text colors | Backdrop index (G7: direct byte), text FG/BG | ST | `test_render` backdrop/palette |
| R#8 | Display control | Stored; D7/D6/D4 (MS/LP/CB) deleted (mask 0x2F) | MR | mask table |
| R#9 | LN/IL/EO | Line count (192/212), interlace | ST | `test_render` interlace |
| R#13 | Blink ON/OFF | Text 2 blink phase nibbles | MR | frame committer |
| R#12 | Blink colors | Text 2 blink FG/BG | MR | `render_text` |
| R#14 | VRAM address A16–A14 | High 3 address bits + auto-carry | ST | `test_core_internal` boundary |
| R#15 | Status pointer | Selects S#0–S#9 | ST | `test_core` status select |
| R#16 | Palette pointer | Palette index, auto-advance | ST | `test_render` palette |
| R#17 | Indirect pointer | Target + auto-increment; self-write rejected (mask 0xBF) | ST | `test_core` indirect |
| R#18 | Display adjust | Stored; not yet applied to active region | MR | see deviations |
| R#19 | Horizontal interrupt line | Compared per scanline for FH | MR | `commit_status` |
| R#23 | Vertical scroll/start | Applied as source-line offset | MR | scanline mapper |
| R#25 | CMD/YJK/YAE/MSK/SP2 | YJK/YAE/MSK/CMD select bits | ST | `test_render`, `test_commands` |
| R#26 | Horizontal coarse scroll | Coarse units (8 / 16 in G5/G6) | MR | scroll mapper |
| R#27 | Horizontal fine scroll | Fine units (1 / 2 in G5/G6) | MR | scroll mapper |
| R#20–22,R#24 | (unused/external) | Stored, no active behavior | MR | mask path |
| R#32–R#46 | Command parameters | Command coordinates/extents/color/arg/code | ST | `test_commands` |
| R#28–R#31, R#47–R#63 | Unsupported numbers | Writes ignored | ST | `test_core_internal` |

## Status registers

| Reg | Implemented behavior | Status | Evidence |
|---|---|---|---|
| S#0 | F (vblank), 5S, collision, 5th-sprite number; read-to-clear of F/5S/C | ST | `test_render`, `test_commands` |
| S#1 | V9958 identification, FH horizontal interrupt | ST | `test_core` identity |
| S#2 | CE (D0), TR (D7), BD (D4) | ST | `test_commands` |
| S#3–S#6 | Collision X/Y | MR | `commit_status` |
| S#7 | Command color (LMCM/POINT) | ST | `test_commands` LMCM/POINT |
| S#8–S#9 | Search X result | ST | `test_commands` SRCH |
| Mouse/light-pen fields | Deleted; remain zero | MR | reset/status path |

## Palette

| Aspect | Behavior | Status | Evidence |
|---|---|---|---|
| 16 entries, 3-bit RGB | Two-byte port commit, R#16 advance | ST | `test_render`, `test_core` |
| Reset palette | Standard MSX2 16 colors | ST | `test_core_internal` reset |
