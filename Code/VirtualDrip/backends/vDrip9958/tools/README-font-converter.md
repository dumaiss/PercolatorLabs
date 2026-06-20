# sfd_to_cp850.py — Font to CP850 ROM Converter

Converts a vector font (FontForge `.sfd`, TrueType `.ttf`, or OpenType `.otf`) into:

1. A **CP850-indexed 6×8 monochrome font ROM binary** (2048 bytes).
2. A **PNG contact sheet** showing all 256 generated glyphs.
3. A **JSON conversion report** with diagnostics and statistics.

Designed for embedded terminal font generation as part of a repeatable build.

---

## Quick Start

```bash
# Create a Python virtual environment
python3 -m venv .venv
. .venv/bin/activate

# Install Python dependencies
pip install -r tools/requirements-font-converter.txt

# Ensure FontForge is installed (required for .sfd inputs)
# Debian/Ubuntu: sudo apt install fontforge
# Arch:          sudo pacman -S fontforge
# macOS:         brew install fontforge

# Convert a font
python3 tools/sfd_to_cp850.py path/to/font.sfd \
    --output-bin build/font.bin \
    --output-png build/font.png \
    --output-report build/font.json
```

---

## Dependencies

| Dependency     | Purpose                                      |
|---------------|----------------------------------------------|
| FontForge CLI | Parse `.sfd` and generate temporary `.ttf`   |
| freetype-py   | Glyph rasterization and font metrics         |
| Pillow        | PNG contact sheet generation                 |
| pytest        | Running the test suite                       |

FontForge is a **system dependency** because its CLI opens the SFD and generates a temporary TTF. The Python packages are installed via `pip`.

---

## Command-Line Interface

```
python3 tools/sfd_to_cp850.py INPUT [OPTIONS]

Required:
  INPUT                  Input font (.sfd, .ttf, or .otf)
  --output-bin PATH      Output ROM binary (2048 bytes)
  --output-png PATH      Output PNG contact sheet
  --output-report PATH   Output JSON report

Rasterization:
  --width INT            Glyph width in pixels (default: 6)
  --height INT           Glyph height in pixels (default: 8)
  --baseline INT         Baseline row, 0-based from top (default: 7)
  --oversample INT       Oversampling factor for grayscale (default: 4)
  --threshold INT        Grayscale threshold 0–255 (default: 128)
  --render-mode MODE     grayscale or mono (default: grayscale)
  --x-align MODE         center or bearing (default: center)

Control Characters:
  --control-policy POL   blank, ascii-visible, or cp437-symbols (default: cp437-symbols)

PNG Output:
  --swatch-scale INT     PNG swatch pixel scale (default: 12)
  --output-raw-png PATH  Optional raw unlabelled atlas PNG

Quality Control:
  --strict               Fail on missing/clipped glyphs or wrong ROM size
  --overrides PATH       JSON bitmap override file
  --verbose              Verbose output to stderr
```

### Example: Typical Build Invocation

```bash
python3 tools/sfd_to_cp850.py Lexis-Regular.sfd \
    --output-bin build/lexis-cp850-6x8.bin \
    --output-png build/lexis-cp850-6x8.png \
    --output-report build/lexis-cp850-6x8.json \
    --width 6 \
    --height 8 \
    --baseline 7 \
    --oversample 4 \
    --threshold 128 \
    --render-mode grayscale \
    --control-policy cp437-symbols \
    --swatch-scale 12 \
    --strict
```

---

## ROM Binary Format

The output binary contains exactly **2048 bytes** (256 glyphs × 8 rows × 1 byte per row).

### Glyph Order

Glyphs are indexed by CP850 byte value:

| Offset (bytes) | Content                       |
|---------------|-------------------------------|
| 0–7           | Glyph `0x00`, rows 0–7        |
| 8–15          | Glyph `0x01`, rows 0–7        |
| ...           | ...                           |
| 2040–2047     | Glyph `0xFF`, rows 0–7        |

### Row Bit Layout (MSB-left, 6 pixels wide)

Within each row byte:

| Bit  | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---------|---|---|---|---|---|---|---|---|
| Pixel | 0 | 1 | 2 | 3 | 4 | 5 | 0 | 0 |

- Leftmost pixel → bit 7 (MSB)
- Rightmost pixel → bit 2
- Bits 1 and 0 are **always zero**

Example: pixels `1 0 1 1 0 1` → byte `10110100` (`0xB4`).

---

## CP850 Mapping Explanation

CP850 (Code Page 850) is a DOS/OEM character set used in many embedded and terminal applications. It encodes 256 characters where:

- **0x00–0x1F** and **0x7F**: Control codes
- **0x20–0x7E**: ASCII printable (same as ASCII)
- **0x80–0xFF**: Extended characters (accented letters, box drawing, symbols)

The converter maps each CP850 byte to its corresponding Unicode code point using Python's built-in `"cp850"` codec:

```python
character = bytes([slot]).decode("cp850")
codepoint = ord(character)
```

Examples:

| CP850 | Unicode | Character                |
|-------|---------|--------------------------|
| 0x41  | U+0041  | LATIN CAPITAL LETTER A   |
| 0x80  | U+00C7  | Ç (C with cedilla)       |
| 0x82  | U+00E9  | é (e with acute)         |
| 0xB3  | U+2502  | │ (box drawings vertical)|
| 0xC4  | U+2500  | ─ (box drawings horiz)   |
| 0xDB  | U+2588  | █ (full block)           |
| 0xE1  | U+00DF  | ß (sharp s)              |

The font's Unicode charmap is used to look up each glyph. Glyph order in the SFD file is irrelevant.

---

## Control-Character Policies

CP850 bytes `0x00–0x1F` and `0x7F` are control codes. The `--control-policy` option controls how these slots appear in the ROM:

### `blank` (default: leave empty)
All control slots contain zero-filled bitmaps. The terminal's control processing is unaffected. This is the most conservative setting.

### `ascii-visible`
Maps control characters to visible ASCII glyphs for debugging:

| Control | Maps To |
|---------|---------|
| 0x00    | @ (U+0040) |
| 0x01    | A (U+0041) |
| ...     | ... |
| 0x1A    | Z (U+005A) |
| 0x1B    | [ (U+005B) |
| ...     | ... |
| 0x1F    | _ (U+005F) |
| 0x7F    | ⌂ (U+2302) |

### `cp437-symbols`
Maps control characters to the traditional CP437 terminal symbols (smiley faces, card suits, arrows, etc.) for a classic DOS terminal look.

**Important**: Control-byte visibility in the ROM does **not** change terminal control processing. The ROM glyph is only displayed if the terminal software explicitly renders that byte as a character. Normal control code handling (line feed, carriage return, etc.) is unaffected.

---

## Rasterization Modes

### `grayscale` (recommended)
- Renders at `oversample × cell_size` resolution.
- Thresholds the grayscale result to produce the final bitmap.
- Higher oversampling gives more accurate results at the cost of speed.
- Works well for most fonts.

### `mono`
- Uses FreeType's native monochrome renderer.
- Requires `--oversample 1`.
- Produces a sharper but potentially less accurate result.
- Correctly unpacks FreeType's MSB-first packed bitmaps.

---

## Fixed-Cell Placement

Every glyph is placed into a fixed cell of exactly `width × height` pixels:

1. The glyph is rasterized at the specified size.
2. Placed into the cell based on `--x-align`:
   - `center`: Horizontally centered in the cell.
   - `bearing`: Placed using the glyph's left side-bearing from FreeType.
3. The baseline from `--baseline` is used for vertical placement.
4. Pixels that fall outside the cell are **clipped** and reported.

This ensures a consistent advancing width for all characters, as required for terminal-cell fonts.

---

## How to Inspect the PNG

The PNG contact sheet is a 16×16 grid in CP850 byte order:

```
Row 0: 0x00 0x01 0x02 ... 0x0F
Row 1: 0x10 0x11 0x12 ... 0x1F
...
Row F: 0xF0 0xF1 0xF2 ... 0xFF
```

Each cell shows:
- The rendered glyph at `--swatch-scale` nearest-neighbor enlargement.
- A hexadecimal label (`00`–`FF`) below each glyph.
- White-on-black display for clear inspection.

Use the contact sheet to check for:
- Broken or missing accents
- Clipped glyphs
- Duplicate or near-duplicate glyphs
- Disconnected box-drawing characters
- Inconsistent baseline alignment
- Glyphs that are too wide for the cell

---

## How to Create Manual Overrides

When the automatic rasterization produces unsatisfactory glyphs, create a JSON override file:

```json
{
  "0x41": [
    "0x30",
    "0x48",
    "0x84",
    "0xFC",
    "0x84",
    "0x84",
    "0x84",
    "0x00"
  ],
  "0xB3": [
    "0x20",
    "0x20",
    "0x20",
    "0x20",
    "0x20",
    "0x20",
    "0x20",
    "0x20"
  ]
}
```

Rules:
- Each key is a CP850 byte slot (`"0x00"`–`"0xFF"`).
- Each value is an array of exactly 8 row bytes.
- Each row byte must be `0x00`–`0xFF`.
- Bits 1 and 0 must be zero for 6-pixel-wide fonts (mask `0xFC`).
- Overrides replace the rasterized glyph in both the ROM and PNG.

Apply overrides with:

```bash
python3 tools/sfd_to_cp850.py font.sfd \
    --output-bin font.bin \
    --output-png font.png \
    --output-report font.json \
    --overrides overrides.json
```

---

## Conversion Report

The JSON report contains diagnostics to help you evaluate font quality:

```json
{
  "input_font": "Lexis-Regular.sfd",
  "input_type": "sfd",
  "font_family": "Lexis",
  "font_style": "Regular",
  "width": 6,
  "height": 8,
  "baseline": 7,
  "oversample": 4,
  "threshold": 128,
  "render_mode": "grayscale",
  "control_policy": "cp437-symbols",
  "missing": [],
  "empty": [],
  "clipped": {
    "left": [],
    "right": [],
    "top": [],
    "bottom": []
  },
  "overridden": [],
  "duplicate_bitmaps": [],
  "rom_size": 2048
}
```

Key fields:
- **missing**: Slots where the font has no glyph for the required Unicode code point.
- **empty**: Printable slots that rendered as all-blank (possible rendering issue).
- **clipped**: Glyphs that extended beyond the cell boundary on any edge.
- **overridden**: Slots replaced by the override file.
- **duplicate_bitmaps**: Groups of slots that ended up with identical 8-row bitmaps (may indicate fallback or shared glyphs).

A text summary is also printed to stdout.

### Strict Mode (`--strict`)

With `--strict`, the tool exits non-zero if:
- Required printable CP850 glyphs are missing.
- Any printable glyph is clipped.
- ROM size is not exactly 2048 bytes.
- The source font lacks a Unicode charmap.
- The override file is invalid.

Without `--strict`, outputs are generated and problems are reported in the JSON.

---

## Running Tests

```bash
# Install test dependencies
pip install -r tools/requirements-font-converter.txt

# Run unit tests (no FontForge required)
pytest tests/test_sfd_to_cp850.py -v -m "not integration"

# Run all tests including integration (requires fontforge + Lexis font)
pytest tests/test_sfd_to_cp850.py -v

# Run with coverage
pytest tests/test_sfd_to_cp850.py -v --cov=tools.sfd_to_cp850 --cov-report=term-missing
```

---

## Troubleshooting

### "fontforge executable not found"

Install FontForge:

```bash
# Debian/Ubuntu
sudo apt install fontforge

# Arch Linux
sudo pacman -S fontforge

# macOS
brew install fontforge
```

For `.ttf` and `.otf` inputs, FontForge is not needed.

### "FreeType cannot open font"

- Verify the input file path is correct.
- For `.sfd` inputs, check that FontForge is installed and the SFD is valid.
- Try converting to `.ttf` first: `fontforge -lang=ff -c 'Open($1); Generate($2)' input.sfd output.ttf`

### "No Unicode charmap"

The font lacks a Unicode character mapping table. This is unusual for modern fonts but can occur with symbol fonts or very old formats. Try generating a new TTF from the SFD with FontForge.

### "Glyphs appear clipped or misaligned"

- Adjust `--baseline` to move glyphs up or down.
- Try `--x-align bearing` if centered placement clips the left side.
- Increase `--width` or `--height` if the font naturally requires more space.
- Use `--oversample 4` or higher for better grayscale rendering.
- Adjust `--threshold` to control how dark a pixel must be to become on.

### "Duplicate bitmaps detected"

This is informational — it means two or more CP850 slots have identical rendered bitmaps. This can happen when:
- The font maps multiple code points to the same glyph (common for box-drawing characters).
- Glyphs are missing and fall back to a default.
- The threshold eliminates subtle differences.

Review the contact sheet to decide if manual overrides are needed.

---

## Design Principles

- **Deterministic**: Given the same inputs, the tool always produces identical outputs.
- **Honest**: Rasterization defects are reported, not silently hidden.
- **Build-friendly**: Suitable for CI/CD pipelines with `--strict` mode.
- **Documented**: All parameters, formats, and error conditions are explained.
- **Testable**: Core logic is unit-tested without requiring font files.
