# Display Mode Support

Status legend: **ST** = implemented + smoke-tested, **MR** = implemented +
manually reviewed. Output pixels are `0x00RRGGBB`. Heights are per field
(progressive); interlace weaves two fields into 384/424 output lines.

## Modes

| Mode | Width × Height | Pixel format | Sprite mode | Status | Evidence |
|---|---|---|---|---|---|
| TEXT1 | 256 × 192/212 | 40×6 text cells, R#7 colors | none | MR | `render_text` |
| TEXT2 | 512 × 192/212 | 80×6 cells, blink (R#12) | none | MR | `render_text` |
| MULTICOLOR | 256 × 192/212 | 4×4 color blocks | 1 | MR | `render_multicolor` |
| GRAPHIC1 | 256 × 192/212 | 1bpp pattern + color table | 1 | ST | `test_render` (sprite/palette) |
| GRAPHIC2 | 256 × 192/212 | 1bpp pattern, 3 banks | 1 | MR | `render_pattern` |
| GRAPHIC3 | 256 × 192/212 | 1bpp pattern, 3 banks | 2 | ST | `test_render` sprite mode 2 |
| GRAPHIC4 | 256 × 192/212 | packed 4-bit palette | 2 | ST | `test_render` palette pixels |
| GRAPHIC5 | 512 × 192/212 | packed 2-bit palette | 2 | ST | `test_render` width |
| GRAPHIC6 | 512 × 192/212 | packed 4-bit palette | 2 | ST | `test_render` width |
| GRAPHIC7 | 256 × 192/212 | direct GRB332 / YJK / YAE | 2 | ST | `test_render` direct + YJK |
| INVALID | retained geometry | border fill | none | ST | `test_render`, `test_core` |

## Color

| Path | Behavior | Status | Evidence |
|---|---|---|---|
| Programmable palette | 3-bit → 8-bit `(c·255+3)/7` | ST | `test_render` |
| Graphic 7 direct | GRB332, 3-bit G/R + 2-bit B | ST | `test_render` |
| YJK | 4-pixel group, `B=(5Y−2J−K)/4`, clamp 0..31 | ST | `test_render` gray invariant |
| YAE | attribute pixels use palette | MR | `render_direct` |

## Sprites / scroll / interlace / border

| Feature | Behavior | Status | Evidence |
|---|---|---|---|
| Sprite mode 1 | ≤4/line, 1 color, 8×8/16×16, mag, early clock | ST | `test_render` |
| Sprite mode 2 | ≤8/line, per-line color, collision/overflow | ST | `test_render` |
| Collision / overflow | S#0 flags + S#3/S#5 / 5th-sprite number | ST | `test_render` collision |
| Horizontal scroll | R#26/R#27 coarse/fine, mode units; single-page | MR | scroll mapper |
| MSK / SP2 | Left-edge mask; two-page wrap not yet implemented | MR | see deviations |
| Interlace | Woven 384/424; even→field0, odd→field1 | ST | `test_render` metadata |
| Border / blank | R#7 backdrop; blanked output stays border | MR | orchestrator |

See [deviations.md](deviations.md) for known gaps (R#18 adjust, two-page scroll,
exact sprite tiling).
