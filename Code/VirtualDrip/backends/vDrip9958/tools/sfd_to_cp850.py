#!/usr/bin/env python3
"""
sfd_to_cp850.py — Convert a vector font (SFD/TTF/OTF) into a CP850-indexed
6x8 monochrome font ROM binary, a PNG contact sheet, and a JSON conversion report.

Usage:
    python3 tools/sfd_to_cp850.py input.sfd --output-bin font.bin --output-png font.png --output-report report.json

Input formats:
    .sfd  — FontForge SplineFontDB (converted to TTF via fontforge CLI)
    .ttf  — TrueType font (read directly by FreeType)
    .otf  — OpenType font (read directly by FreeType)

Outputs:
    --output-bin    PATH    2048-byte ROM binary (256 glyphs x 8 rows x 1 byte)
    --output-png    PATH    PNG contact sheet (labelled 16x16 grid at swatch scale)
    --output-report PATH    JSON conversion report
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Sequence, Set, TextIO, Tuple, Union

import freetype
from PIL import Image, ImageDraw, ImageFont


# ---------------------------------------------------------------------------
# CP850 byte-to-Unicode mapping
# ---------------------------------------------------------------------------

def cp850_unicode(slot: int) -> int:
    """Return the Unicode code point for a CP850 byte value (0x00–0xFF)."""
    if not (0 <= slot <= 0xFF):
        raise ValueError(f"CP850 slot out of range: {slot:#04x}")
    character = bytes([slot]).decode("cp850")
    return ord(character)


def is_control_byte(value: int) -> bool:
    """Return True if *value* is a CP850 control byte (0x00–0x1F or 0x7F)."""
    return value < 0x20 or value == 0x7F


# ---------------------------------------------------------------------------
# Control-character policy Unicode mappings
# ---------------------------------------------------------------------------

# ASCII-visible: map control char 0x00-0x1F to U+0040..U+005F (@, A-Z, etc.)
_ASCII_VISIBLE_MAP: Dict[int, int] = {
    0x00: 0x0040, 0x01: 0x0041, 0x02: 0x0042, 0x03: 0x0043,
    0x04: 0x0044, 0x05: 0x0045, 0x06: 0x0046, 0x07: 0x0047,
    0x08: 0x0048, 0x09: 0x0049, 0x0A: 0x004A, 0x0B: 0x004B,
    0x0C: 0x004C, 0x0D: 0x004D, 0x0E: 0x004E, 0x0F: 0x004F,
    0x10: 0x0050, 0x11: 0x0051, 0x12: 0x0052, 0x13: 0x0053,
    0x14: 0x0054, 0x15: 0x0055, 0x16: 0x0056, 0x17: 0x0057,
    0x18: 0x0058, 0x19: 0x0059, 0x1A: 0x005A, 0x1B: 0x005B,
    0x1C: 0x005C, 0x1D: 0x005D, 0x1E: 0x005E, 0x1F: 0x005F,
    0x7F: 0x2302,  # DEL → ⌂ (HOUSE)
}

# CP437-symbols: map control chars to CP437's common symbol placements.
# CP437 uses 0x00–0x1F for control chars but also assigns glyphs at those slots
# for terminals that render them.  We map the control byte to the Unicode
# code point that CP437 traditionally associates with that slot.
_CP437_SYMBOL_MAP: Dict[int, int] = {
    0x01: 0x263A,  # ☺ white smiling face
    0x02: 0x263B,  # ☻ black smiling face
    0x03: 0x2665,  # ♥ black heart suit
    0x04: 0x2666,  # ♦ black diamond suit
    0x05: 0x2663,  # ♣ black club suit
    0x06: 0x2660,  # ♠ black spade suit
    0x07: 0x2022,  # • bullet
    0x08: 0x25D8,  # ◘ inverse bullet
    0x09: 0x25CB,  # ○ white circle
    0x0A: 0x25D9,  # ◙ inverse white circle
    0x0B: 0x2642,  # ♂ male sign
    0x0C: 0x2640,  # ♀ female sign
    0x0D: 0x266A,  # ♪ eighth note
    0x0E: 0x266B,  # ♫ beamed eighth notes
    0x0F: 0x263C,  # ☼ white sun with rays
    0x10: 0x25BA,  # ► black right-pointing pointer
    0x11: 0x25C4,  # ◄ black left-pointing pointer
    0x12: 0x2195,  # ↕ up down arrow
    0x13: 0x203C,  # ‼ double exclamation mark
    0x14: 0x00B6,  # ¶ pilcrow sign
    0x15: 0x00A7,  # § section sign
    0x16: 0x25AC,  # ▬ black rectangle
    0x17: 0x21A8,  # ↨ up down arrow with base
    0x18: 0x2191,  # ↑ upwards arrow
    0x19: 0x2193,  # ↓ downwards arrow
    0x1A: 0x2192,  # → rightwards arrow
    0x1B: 0x2190,  # ← leftwards arrow
    0x1C: 0x221F,  # ∟ right angle
    0x1D: 0x2194,  # ↔ left right arrow
    0x1E: 0x25B2,  # ▲ black up-pointing triangle
    0x1F: 0x25BC,  # ▼ black down-pointing triangle
}


def control_unicode(slot: int, policy: str) -> Optional[int]:
    """Return the Unicode code point to render for a control *slot*
    given *policy* (blank, ascii-visible, or cp437-symbols).

    Returns None when the policy is blank (no glyph should be rendered).
    """
    if not is_control_byte(slot):
        raise ValueError(f"slot {slot:#04x} is not a control byte")

    if policy == "blank":
        return None
    elif policy == "ascii-visible":
        return _ASCII_VISIBLE_MAP.get(slot)
    elif policy == "cp437-symbols":
        return _CP437_SYMBOL_MAP.get(slot)
    else:
        raise ValueError(f"Unknown control policy: {policy!r}")


def target_unicode(slot: int, control_policy: str) -> Optional[int]:
    """Return the Unicode code point to look up in the font for *slot*.

    For printable CP850 bytes, returns the CP850 Unicode mapping.
    For control bytes, applies the *control_policy*.
    Returns None when no glyph should be rendered (blank control policy entry).
    """
    if is_control_byte(slot):
        return control_unicode(slot, control_policy)
    return cp850_unicode(slot)


# ---------------------------------------------------------------------------
# Row packing
# ---------------------------------------------------------------------------

def pack_row_msb_left(pixels: Sequence[bool], width: int = 6) -> int:
    """Pack *width* boolean pixels into a single byte.

    Leftmost pixel  → bit 7
    Second pixel    → bit 6
    ...
    Rightmost pixel → bit (8 - width)

    Unused low bits are zero.
    """
    if len(pixels) < width:
        raise ValueError(f"Need at least {width} pixels, got {len(pixels)}")
    if width < 1 or width > 8:
        raise ValueError(f"width must be 1..8, got {width}")

    result = 0
    for i in range(width):
        if pixels[i]:
            result |= 1 << (7 - i)
    return result


def pack_row_msb_left_from_int(row_int: int, width: int = 6) -> int:
    """Pack a row from an integer where bits 7..(8-width) represent pixels.

    Bits 1 and 0 are masked to zero.
    """
    mask = ((1 << width) - 1) << (8 - width)
    return row_int & mask


# ---------------------------------------------------------------------------
# Configuration dataclass
# ---------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class ConverterConfig:
    """Immutable configuration for the font conversion."""

    input_path: Path
    output_bin: Path
    output_png: Path
    output_report: Path
    output_raw_png: Optional[Path]

    width: int
    height: int
    baseline: int
    oversample: int
    threshold: int
    render_mode: str  # "grayscale" or "mono"
    control_policy: str  # "blank", "ascii-visible", or "cp437-symbols"
    swatch_scale: int
    x_align: str  # "center" or "bearing"
    strict: bool
    overrides_path: Optional[Path]
    verbose: bool

    def validate(self) -> List[str]:
        """Return a list of validation error messages (empty if valid)."""
        errors: List[str] = []
        if self.width < 1 or self.width > 8:
            errors.append(f"--width must be 1..8, got {self.width}")
        if self.height < 1 or self.height > 255:
            errors.append(f"--height must be 1..255, got {self.height}")
        if self.baseline < 0 or self.baseline >= self.height:
            errors.append(f"--baseline must be 0..{self.height - 1}, got {self.baseline}")
        if self.oversample < 1 or self.oversample > 16:
            errors.append(f"--oversample must be 1..16, got {self.oversample}")
        if self.threshold < 0 or self.threshold > 255:
            errors.append(f"--threshold must be 0..255, got {self.threshold}")
        if self.render_mode not in ("grayscale", "mono"):
            errors.append(f"--render-mode must be grayscale or mono, got {self.render_mode!r}")
        if self.control_policy not in ("blank", "ascii-visible", "cp437-symbols"):
            errors.append(f"--control-policy must be blank, ascii-visible, or cp437-symbols, got {self.control_policy!r}")
        if self.swatch_scale < 1 or self.swatch_scale > 64:
            errors.append(f"--swatch-scale must be 1..64, got {self.swatch_scale}")
        if self.x_align not in ("center", "bearing"):
            errors.append(f"--x-align must be center or bearing, got {self.x_align!r}")
        if self.render_mode == "mono" and self.oversample != 1:
            errors.append("mono render mode requires --oversample 1")
        return errors


# ---------------------------------------------------------------------------
# Conversion report dataclass
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class ConversionReport:
    """Accumulates diagnostics during conversion."""
    input_font: str = ""
    input_type: str = ""
    font_family: str = ""
    font_style: str = ""
    width: int = 6
    height: int = 8
    baseline: int = 7
    oversample: int = 1
    threshold: int = 0
    render_mode: str = "mono"
    control_policy: str = "blank"
    missing: List[str] = dataclasses.field(default_factory=list)
    empty: List[str] = dataclasses.field(default_factory=list)
    clipped: Dict[str, List[str]] = dataclasses.field(default_factory=lambda: {
        "left": [], "right": [], "top": [], "bottom": []
    })
    overridden: List[str] = dataclasses.field(default_factory=list)
    duplicate_bitmaps: List[List[str]] = dataclasses.field(default_factory=list)
    rom_size: int = 2048

    def to_dict(self) -> Dict[str, Any]:
        return dataclasses.asdict(self)


# ---------------------------------------------------------------------------
# Font loading (SFD → TTF via FontForge, or direct TTF/OTF)
# ---------------------------------------------------------------------------

def _find_fontforge() -> Optional[str]:
    """Return the path to the fontforge executable, or None."""
    return shutil.which("fontforge")


def _sfd_to_ttf(sfd_path: Path, ttf_path: Path) -> None:
    """Convert an SFD file to TTF using the FontForge command line."""
    fontforge = _find_fontforge()
    if fontforge is None:
        raise FileNotFoundError(
            "fontforge executable not found. Install FontForge:\n"
            "  Debian/Ubuntu: sudo apt install fontforge\n"
            "  Arch:          sudo pacman -S fontforge\n"
            "  macOS:         brew install fontforge"
        )

    cmd = [
        fontforge,
        "-lang=ff",
        "-c",
        f'Open($1); Generate($2)',
        str(sfd_path),
        str(ttf_path),
    ]
    result = subprocess.run(
        cmd, capture_output=True, text=True, timeout=120
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"FontForge conversion failed (exit {result.returncode}):\n"
            f"stderr: {result.stderr.strip()}\n"
            f"stdout: {result.stdout.strip()}"
        )
    if not ttf_path.exists() or ttf_path.stat().st_size == 0:
        raise RuntimeError(
            f"FontForge ran but did not produce a valid TTF at {ttf_path}"
        )


def load_font_face(
    input_path: Path,
    oversample: int,
    render_mode: str,
    verbose: bool = False,
) -> Tuple[freetype.Face, str, Path]:
    """Load a FreeType face from *input_path*.

    For .sfd files, converts to a temporary TTF first.
    Returns (face, input_type, resolved_path) where resolved_path
    is the file actually opened by FreeType.
    """
    suffix = input_path.suffix.lower()
    input_type: str
    resolved_path: Path
    temp_dir: Optional[Path] = None

    if suffix == ".sfd":
        input_type = "sfd"
        temp_dir = Path(tempfile.mkdtemp(prefix="sfd2cp850_"))
        ttf_path = temp_dir / (input_path.stem + ".ttf")
        if verbose:
            print(f"Converting {input_path} → {ttf_path} via fontforge...", file=sys.stderr)
        _sfd_to_ttf(input_path, ttf_path)
        resolved_path = ttf_path
    elif suffix in (".ttf", ".otf"):
        input_type = suffix.lstrip(".")
        resolved_path = input_path
    else:
        raise ValueError(
            f"Unsupported input format: {suffix}. Expected .sfd, .ttf, or .otf."
        )

    try:
        face = freetype.Face(str(resolved_path))
    except freetype.FT_Exception as e:
        raise RuntimeError(f"FreeType cannot open font: {resolved_path}: {e}") from e

    # Set pixel size: we oversample for grayscale mode to get better results
    # before thresholding.  Mono mode uses oversample=1 and native FT monochrome.
    if render_mode == "mono":
        face.set_pixel_sizes(oversample * 8, oversample * 8)
    else:
        face.set_pixel_sizes(oversample * 6, oversample * 8)  # grayscale

    # Verify Unicode charmap exists
    if face.charmap is None:
        # Try to select a Unicode charmap
        found = False
        for idx in range(face.num_charmaps):
            if face.charmaps[idx].encoding == freetype.FT_ENCODING_UNICODE:
                face.set_charmap(face.charmaps[idx])
                found = True
                break
        if not found:
            raise RuntimeError(
                f"Font {input_path.name} has no Unicode charmap. "
                f"Available charmaps: {face.num_charmaps}"
            )

    return face, input_type, resolved_path


# ---------------------------------------------------------------------------
# Glyph rasterization
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class ClippedFlags:
    """Records which edges of a glyph were clipped."""
    left: bool = False
    right: bool = False
    top: bool = False
    bottom: bool = False

    def any_clipped(self) -> bool:
        return self.left or self.right or self.top or self.bottom


def _render_grayscale(
    face: freetype.Face,
    codepoint: int,
    oversample: int,
    width: int,
    height: int,
    baseline: int,
    threshold: int,
    x_align: str,
) -> Tuple[List[List[bool]], ClippedFlags]:
    """Render a glyph using FreeType grayscale rendering, then threshold.

    Returns (pixel_grid, clipped) where pixel_grid is height rows × width columns
    of boolean pixels.
    """
    clipped = ClippedFlags()

    try:
        face.load_char(chr(codepoint), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
    except freetype.FT_Exception:
        # Glyph not found — return blank grid
        return [[False] * width for _ in range(height)], clipped

    bitmap = face.glyph.bitmap
    glyph_width = bitmap.width
    glyph_rows = bitmap.rows
    glyph_buffer = bitmap.buffer

    if glyph_buffer is None or glyph_width == 0 or glyph_rows == 0:
        return [[False] * width for _ in range(height)], clipped

    # FreeType metrics for placement
    # bearingX: distance from origin to left edge of bitmap (can be negative)
    # bearingY: distance from origin to top edge of bitmap (positive = above baseline)
    bearing_x = face.glyph.bitmap_left  # pixels from pen position
    bearing_y = face.glyph.bitmap_top   # pixels above baseline

    # We'll place the glyph into a fixed cell [height × oversampled_height, width × oversampled_width]
    cell_w = width * oversample
    cell_h = height * oversample
    baseline_y = baseline * oversample

    # Sample the oversampled cell at the native glyph rendering resolution.
    # The glyph bitmap is at 1x (the face was set to oversample * width, oversample * height).
    # Actually, since we set pixel sizes to oversample * width, oversample * height,
    # the bitmap is already at the oversampled resolution.
    # So cell_w and cell_h equal the face pixel size, and we directly map.

    # Compute where in cell the glyph bitmap goes.
    # x_offset: horizontal placement
    if x_align == "center":
        x_offset = (cell_w - glyph_width) // 2
    else:  # bearing
        # bearing_x is relative to the pen position (left edge of cell)
        x_offset = bearing_x

    # y_offset: vertical placement. bearing_y is pixels above baseline.
    # In our cell, row 0 = top, so baseline_y rows from top is the baseline.
    # The glyph bitmap top should be at baseline_y - bearing_y.
    y_offset = baseline_y - bearing_y

    # Build the oversampled cell
    buffer_flat: List[int] = list(glyph_buffer)
    grid = [[False] * cell_w for _ in range(cell_h)]

    for gy in range(glyph_rows):
        for gx in range(glyph_width):
            # FreeType bitmap: row-major, each byte is one pixel (grayscale 0-255)
            idx = gy * glyph_width + gx
            if idx < len(buffer_flat):
                pixel_val = buffer_flat[idx]
            else:
                pixel_val = 0

            cx = x_offset + gx
            cy = y_offset + gy

            if 0 <= cx < cell_w and 0 <= cy < cell_h:
                grid[cy][cx] = pixel_val >= threshold
            elif cx < 0:
                clipped.left = True
            elif cx >= cell_w:
                clipped.right = True
            elif cy < 0:
                clipped.top = True
            elif cy >= cell_h:
                clipped.bottom = True

    # Downsample to target width × height by thresholding
    result = [[False] * width for _ in range(height)]
    for row in range(height):
        for col in range(width):
            # Count oversampled pixels in this block
            count = 0
            total = 0
            for dy in range(oversample):
                for dx in range(oversample):
                    sy = row * oversample + dy
                    sx = col * oversample + dx
                    if sy < cell_h and sx < cell_w:
                        total += 1
                        if grid[sy][sx]:
                            count += 1
            if total > 0 and count > total // 2:
                result[row][col] = True

    return result, clipped


def _unpack_mono_row(bitmap_buffer: bytes, row: int, pitch: int, width: int) -> List[bool]:
    """Unpack one row of a FreeType monochrome bitmap.

    FreeType monochrome bitmaps pack pixels MSB-first within each byte.
    *pitch* is the byte stride per row (may be negative).
    *width* is the number of pixels in the row.
    """
    if pitch < 0:
        # Negative pitch means bottom-up bitmap; this is unusual but handle it
        raise NotImplementedError("Negative pitch monochrome bitmaps not supported")

    pixels: List[bool] = []
    byte_offset = row * abs(pitch)
    for col in range(width):
        byte_idx = byte_offset + (col // 8)
        bit_idx = 7 - (col % 8)  # MSB first within each byte
        if byte_idx < len(bitmap_buffer):
            val = bitmap_buffer[byte_idx]
            pixels.append(bool(val & (1 << bit_idx)))
        else:
            pixels.append(False)
    return pixels


def _render_mono(
    face: freetype.Face,
    codepoint: int,
    width: int,
    height: int,
    baseline: int,
    x_align: str,
) -> Tuple[List[List[bool]], ClippedFlags]:
    """Render a glyph using FreeType monochrome rendering."""
    clipped = ClippedFlags()

    try:
        face.load_char(
            chr(codepoint),
            freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO,
        )
    except freetype.FT_Exception:
        return [[False] * width for _ in range(height)], clipped

    bitmap = face.glyph.bitmap
    glyph_width = bitmap.width
    glyph_rows = bitmap.rows
    glyph_buffer = bitmap.buffer

    if glyph_buffer is None or glyph_width == 0 or glyph_rows == 0:
        return [[False] * width for _ in range(height)], clipped

    pitch = bitmap.pitch
    if pitch == 0:
        return [[False] * width for _ in range(height)], clipped

    bearing_x = face.glyph.bitmap_left
    bearing_y = face.glyph.bitmap_top

    cell_w = width
    cell_h = height
    baseline_y = baseline

    if x_align == "center":
        x_offset = (cell_w - glyph_width) // 2
    else:
        x_offset = bearing_x

    y_offset = baseline_y - bearing_y

    glyph_pixels: List[List[bool]] = [
        _unpack_mono_row(glyph_buffer, gy, pitch, glyph_width)
        for gy in range(glyph_rows)
    ]

    result = [[False] * width for _ in range(height)]
    for gy in range(glyph_rows):
        for gx in range(glyph_width):
            cx = x_offset + gx
            cy = y_offset + gy
            if 0 <= cx < cell_w and 0 <= cy < cell_h:
                result[cy][cx] = glyph_pixels[gy][gx]
            elif cx < 0:
                clipped.left = True
            elif cx >= cell_w:
                clipped.right = True
            elif cy < 0:
                clipped.top = True
            elif cy >= cell_h:
                clipped.bottom = True

    return result, clipped


def render_glyph(
    face: freetype.Face,
    codepoint: int,
    config: ConverterConfig,
) -> Tuple[List[List[bool]], ClippedFlags]:
    """Render a single glyph at *codepoint* into a boolean grid.

    Returns (pixels, clipped) where pixels is config.height rows × config.width columns.
    """
    if config.render_mode == "mono":
        return _render_mono(
            face, codepoint,
            config.width, config.height, config.baseline, config.x_align,
        )
    else:
        return _render_grayscale(
            face, codepoint,
            config.oversample, config.width, config.height, config.baseline,
            config.threshold, config.x_align,
        )


# ---------------------------------------------------------------------------
# Override file handling
# ---------------------------------------------------------------------------

def load_overrides(
    overrides_path: Path,
    width: int,
) -> Dict[int, List[int]]:
    """Load and validate a JSON glyph override file.

    Returns a dict mapping CP850 slot → list of 8 row bytes (ints 0-255).
    """
    try:
        with open(overrides_path, "r") as f:
            raw = json.load(f)
    except json.JSONDecodeError as e:
        raise ValueError(f"Invalid JSON in overrides file {overrides_path}: {e}")
    except OSError as e:
        raise OSError(f"Cannot read overrides file {overrides_path}: {e}")

    if not isinstance(raw, dict):
        raise ValueError("Overrides file must contain a JSON object (dict)")

    result: Dict[int, List[int]] = {}
    for key, val in raw.items():
        # Parse key as hex string like "0x41" or decimal string
        if isinstance(key, str):
            try:
                slot = int(key, 0)
            except ValueError:
                raise ValueError(f"Invalid override key: {key!r} — must be hex or decimal integer")
        elif isinstance(key, int):
            slot = key
        else:
            raise ValueError(f"Invalid override key type: {type(key).__name__}")

        if not (0 <= slot <= 0xFF):
            raise ValueError(f"Override slot {key!r} out of range 0x00–0xFF")

        if not isinstance(val, list):
            raise ValueError(f"Override value for {key} must be an array of 8 row hex strings")

        if len(val) != 8:
            raise ValueError(
                f"Override for slot {slot:#04x} must have exactly 8 rows, got {len(val)}"
            )

        rows: List[int] = []
        for i, row_str in enumerate(val):
            if isinstance(row_str, str):
                try:
                    row_val = int(row_str, 0)
                except ValueError:
                    raise ValueError(
                        f"Override slot {slot:#04x} row {i}: invalid hex value {row_str!r}"
                    )
            elif isinstance(row_str, int):
                row_val = row_str
            else:
                raise ValueError(
                    f"Override slot {slot:#04x} row {i}: expected hex string or int, got {type(row_str).__name__}"
                )

            if not (0 <= row_val <= 0xFF):
                raise ValueError(
                    f"Override slot {slot:#04x} row {i}: value {row_val:#04x} out of range 0x00–0xFF"
                )

            # Validate that bits 1 and 0 are zero for the given width
            low_bits_mask = (1 << (8 - width)) - 1
            if row_val & low_bits_mask:
                raise ValueError(
                    f"Override slot {slot:#04x} row {i}: value {row_val:#04x} has "
                    f"non-zero bits in positions 1..0 (mask {low_bits_mask:#04x}) — "
                    f"these must be zero for width={width}"
                )

            rows.append(row_val)

        result[slot] = rows

    return result


# ---------------------------------------------------------------------------
# ROM construction
# ---------------------------------------------------------------------------

def build_rom(
    glyphs: Dict[int, List[List[bool]]],
    overrides: Dict[int, List[int]],
    width: int,
) -> bytes:
    """Build the 2048-byte ROM from rendered glyphs and overrides.

    *glyphs* maps CP850 slot → boolean pixel grid (height rows × width cols).
    *overrides* maps CP850 slot → list of 8 packed row bytes.
    """
    rom = bytearray(256 * 8)

    for slot in range(256):
        base = slot * 8
        if slot in overrides:
            for row in range(8):
                rom[base + row] = overrides[slot][row]
        elif slot in glyphs:
            for row in range(8):
                pixels = glyphs[slot][row]
                rom[base + row] = pack_row_msb_left(pixels, width)
        else:
            # Slot not rendered — leave as zeros
            pass

    return bytes(rom)


def int_to_pixels(value: int, width: int = 6) -> List[bool]:
    """Convert a packed row byte to a list of booleans."""
    return [bool(value & (1 << (7 - i))) for i in range(width)]


# ---------------------------------------------------------------------------
# PNG contact sheet generation
# ---------------------------------------------------------------------------

def _draw_labeled_grid(
    glyphs: Dict[int, List[List[bool]]],
    config: ConverterConfig,
) -> Image.Image:
    """Generate a labelled 16×16 contact sheet PNG."""
    width = config.width
    height = config.height
    scale = config.swatch_scale

    # Layout constants
    glyph_display_w = width * scale
    glyph_display_h = height * scale
    label_height = 10 * scale  # room for hex label
    margin = 2
    grid_line = 1

    cell_w = glyph_display_w + margin * 2
    cell_h = glyph_display_h + label_height + margin * 3

    cols = 16
    rows = 16

    img_w = cols * cell_w + (cols + 1) * grid_line
    img_h = rows * cell_h + (rows + 1) * grid_line

    # Create image with white background
    img = Image.new("L", (img_w, img_h), color=255)
    draw = ImageDraw.Draw(img)

    # Try to load a small monospace font for labels
    try:
        # Use a built-in default font if available; PIL's default is very small
        label_font = ImageFont.load_default()
    except Exception:
        label_font = None

    for grid_row in range(rows):
        for grid_col in range(cols):
            slot = grid_row * 16 + grid_col

            # Cell origin
            cx = grid_col * cell_w + (grid_col + 1) * grid_line + margin
            cy = grid_row * cell_h + (grid_row + 1) * grid_line + margin

            # Draw grid outline
            draw.rectangle(
                [cx - margin, cy - margin, cx + glyph_display_w + margin - 1,
                 cy + glyph_display_h + label_height + margin - 1],
                outline=0,
            )

            # Draw label
            label = f"{slot:02X}"
            label_y = cy + glyph_display_h + margin
            if label_font:
                draw.text((cx, label_y), label, fill=0, font=label_font)
            else:
                draw.text((cx, label_y), label, fill=0)

            # Draw glyph pixels
            # White pixels (value 0=black, 255=white in L mode) — we invert:
            # Foreground = black, background = white
            pixel_data = glyphs.get(slot)
            if pixel_data:
                for gy in range(height):
                    for gx in range(width):
                        if pixel_data[gy][gx]:
                            px = cx + gx * scale
                            py = cy + gy * scale
                            draw.rectangle(
                                [px, py, px + scale - 1, py + scale - 1],
                                fill=0,  # black for foreground
                            )

    return img


def _draw_raw_atlas(
    glyphs: Dict[int, List[List[bool]]],
    config: ConverterConfig,
) -> Image.Image:
    """Generate a raw unlabelled 96×128 atlas (before scaling)."""
    width = config.width
    height = config.height
    cols = 16
    rows = 16

    img_w = cols * width
    img_h = rows * height

    img = Image.new("L", (img_w, img_h), color=255)

    for grid_row in range(rows):
        for grid_col in range(cols):
            slot = grid_row * 16 + grid_col
            pixel_data = glyphs.get(slot)
            if pixel_data:
                for gy in range(height):
                    for gx in range(width):
                        if pixel_data[gy][gx]:
                            px = grid_col * width + gx
                            py = grid_row * height + gy
                            img.putpixel((px, py), 0)

    # Scale up with nearest-neighbor
    img = img.resize(
        (img_w * config.swatch_scale, img_h * config.swatch_scale),
        Image.NEAREST,
    )
    return img


def generate_png(
    glyphs: Dict[int, List[List[bool]]],
    config: ConverterConfig,
    output_path: Path,
) -> None:
    """Generate and save the labelled contact sheet PNG."""
    img = _draw_labeled_grid(glyphs, config)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(output_path, "PNG")


def generate_raw_png(
    glyphs: Dict[int, List[List[bool]]],
    config: ConverterConfig,
    output_path: Path,
) -> None:
    """Generate and save the raw unlabelled atlas PNG."""
    img = _draw_raw_atlas(glyphs, config)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(output_path, "PNG")


# ---------------------------------------------------------------------------
# Duplicate bitmap detection
# ---------------------------------------------------------------------------

def detect_duplicates(
    rom: bytes,
    control_policy: str,
) -> List[List[str]]:
    """Group CP850 slots that have identical 8-row bitmaps in *rom*.

    Returns a list of lists, each inner list containing slot hex strings
    that share the same bitmap.  Only groups of size >= 2 are returned.

    When *control_policy* is "blank", blank control slots are excluded from
    duplicate detection since they are intentionally empty.
    """
    from collections import defaultdict

    bitmap_to_slots: Dict[bytes, List[int]] = defaultdict(list)

    for slot in range(256):
        base = slot * 8
        bitmap = rom[base:base + 8]

        # Skip all-zero control bytes when policy is blank
        if control_policy == "blank" and is_control_byte(slot) and bitmap == b"\x00" * 8:
            continue

        bitmap_to_slots[bitmap].append(slot)

    duplicates: List[List[str]] = []
    for slots in bitmap_to_slots.values():
        if len(slots) >= 2:
            duplicates.append([f"0x{s:02X}" for s in sorted(slots)])

    duplicates.sort(key=lambda g: g[0])
    return duplicates


# ---------------------------------------------------------------------------
# Conversion report generation
# ---------------------------------------------------------------------------

def generate_report(
    glyphs: Dict[int, List[List[bool]]],
    glyph_clipped: Dict[int, ClippedFlags],
    overrides: Dict[int, List[int]],
    rom: bytes,
    config: ConverterConfig,
    face: freetype.Face,
    input_type: str,
) -> ConversionReport:
    """Generate the conversion report."""
    report = ConversionReport(
        input_font=str(config.input_path),
        input_type=input_type,
        width=config.width,
        height=config.height,
        baseline=config.baseline,
        oversample=config.oversample,
        threshold=config.threshold,
        render_mode=config.render_mode,
        control_policy=config.control_policy,
        rom_size=len(rom),
    )

    # Font metadata
    try:
        report.font_family = face.family_name.decode("utf-8", errors="replace") if face.family_name else ""
    except Exception:
        report.font_family = ""
    try:
        report.font_style = face.style_name.decode("utf-8", errors="replace") if face.style_name else ""
    except Exception:
        report.font_style = ""

    for slot in range(256):
        slot_hex = f"0x{slot:02X}"

        # Missing: no Unicode target or glyph not rendered
        target = target_unicode(slot, config.control_policy)
        if target is None:
            # Control policy said blank — not missing
            continue

        if slot not in glyphs:
            report.missing.append(slot_hex)
            continue

        pixels = glyphs[slot]
        # Check if empty (all pixels False)
        is_empty = all(not p for row in pixels for p in row)
        # Whitespace control bytes that are blank by policy are not "empty" errors
        if is_empty and is_control_byte(slot) and config.control_policy == "blank":
            pass
        elif is_empty and not is_control_byte(slot):
            report.empty.append(slot_hex)

        # Clipping
        clipped = glyph_clipped.get(slot, ClippedFlags())
        if clipped.left:
            report.clipped["left"].append(slot_hex)
        if clipped.right:
            report.clipped["right"].append(slot_hex)
        if clipped.top:
            report.clipped["top"].append(slot_hex)
        if clipped.bottom:
            report.clipped["bottom"].append(slot_hex)

    # Overridden
    for slot in sorted(overrides.keys()):
        report.overridden.append(f"0x{slot:02X}")

    # Duplicates
    report.duplicate_bitmaps = detect_duplicates(rom, config.control_policy)

    return report


def print_text_summary(report: ConversionReport, file: TextIO = sys.stdout) -> None:
    """Print a concise text summary of the conversion report."""
    print(f"Input:       {report.input_font}", file=file)
    print(f"Type:        {report.input_type}", file=file)
    print(f"Family:      {report.font_family}", file=file)
    print(f"Style:       {report.font_style}", file=file)
    print(f"Cell:        {report.width}×{report.height}  baseline={report.baseline}", file=file)
    print(f"Oversample:  {report.oversample}", file=file)
    print(f"Threshold:   {report.threshold}", file=file)
    print(f"Render:      {report.render_mode}", file=file)
    print(f"Controls:    {report.control_policy}", file=file)
    print(f"ROM size:    {report.rom_size} bytes", file=file)
    print(f"Missing:     {len(report.missing)}", file=file)
    if report.missing:
        print(f"             {', '.join(report.missing)}", file=file)
    print(f"Empty:       {len(report.empty)}", file=file)
    if report.empty:
        print(f"             {', '.join(report.empty)}", file=file)
    for edge in ("left", "right", "top", "bottom"):
        clipped_list = report.clipped.get(edge, [])
        if clipped_list:
            print(f"Clipped {edge:>6}: {', '.join(clipped_list)}", file=file)
    if report.overridden:
        print(f"Overridden:  {', '.join(report.overridden)}", file=file)
    if report.duplicate_bitmaps:
        print(f"Duplicates:  {len(report.duplicate_bitmaps)} groups", file=file)
        for group in report.duplicate_bitmaps[:5]:  # show first 5 groups
            print(f"             {', '.join(group)}", file=file)
        if len(report.duplicate_bitmaps) > 5:
            print(f"             ... and {len(report.duplicate_bitmaps) - 5} more groups", file=file)


# ---------------------------------------------------------------------------
# Strict validation
# ---------------------------------------------------------------------------

def strict_check(report: ConversionReport, config: ConverterConfig) -> List[str]:
    """Return a list of failures when --strict is enabled."""
    failures: List[str] = []

    # Missing required printable glyphs
    printable_missing = [
        s for s in report.missing
        if not is_control_byte(int(s, 16))
    ]
    if printable_missing:
        failures.append(f"Missing printable glyphs: {', '.join(printable_missing)}")

    # Missing control glyphs that a policy says should exist
    control_missing = [
        s for s in report.missing
        if is_control_byte(int(s, 16))
    ]
    if control_missing and config.control_policy != "blank":
        failures.append(f"Missing control-policy glyphs: {', '.join(control_missing)}")

    # Clipped printable glyphs
    for edge in ("left", "right", "top", "bottom"):
        clipped_slots = report.clipped.get(edge, [])
        printable_clipped = [
            s for s in clipped_slots
            if not is_control_byte(int(s, 16))
        ]
        if printable_clipped:
            failures.append(f"Clipped ({edge}) printable glyphs: {', '.join(printable_clipped)}")

    # ROM size
    if report.rom_size != 2048:
        failures.append(f"ROM size is {report.rom_size}, expected 2048")

    return failures


# ---------------------------------------------------------------------------
# Main conversion pipeline
# ---------------------------------------------------------------------------

def convert(config: ConverterConfig) -> ConversionReport:
    """Run the full conversion pipeline and return the report."""
    # Load font
    face, input_type, resolved_path = load_font_face(
        config.input_path, config.oversample, config.render_mode, config.verbose,
    )

    # Load overrides if specified
    overrides: Dict[int, List[int]] = {}
    if config.overrides_path:
        overrides = load_overrides(config.overrides_path, config.width)

    # Render all 256 slots
    glyphs: Dict[int, List[List[bool]]] = {}
    glyph_clipped: Dict[int, ClippedFlags] = {}

    for slot in range(256):
        target = target_unicode(slot, config.control_policy)
        if target is None:
            # Policy says blank — store empty glyph
            glyphs[slot] = [[False] * config.width for _ in range(config.height)]
            glyph_clipped[slot] = ClippedFlags()
            continue

        try:
            pixels, clipped = render_glyph(face, target, config)
            glyphs[slot] = pixels
            glyph_clipped[slot] = clipped
        except Exception as e:
            if config.verbose:
                print(f"Warning: failed to render slot 0x{slot:02X} (U+{target:04X}): {e}",
                      file=sys.stderr)
            glyphs[slot] = [[False] * config.width for _ in range(config.height)]
            glyph_clipped[slot] = ClippedFlags()

    # Build ROM
    rom = build_rom(glyphs, overrides, config.width)

    # Verify ROM size
    if len(rom) != 2048:
        raise RuntimeError(f"ROM size is {len(rom)}, expected 2048")

    # Generate report
    report = generate_report(glyphs, glyph_clipped, overrides, rom, config, face, input_type)

    # Write ROM binary
    config.output_bin.parent.mkdir(parents=True, exist_ok=True)
    with open(config.output_bin, "wb") as f:
        f.write(rom)

    # Generate PNG contact sheet
    generate_png(glyphs, config, config.output_png)

    # Generate raw atlas if requested
    if config.output_raw_png:
        generate_raw_png(glyphs, config, config.output_raw_png)

    # Write JSON report
    config.output_report.parent.mkdir(parents=True, exist_ok=True)
    with open(config.output_report, "w") as f:
        json.dump(report.to_dict(), f, indent=2, sort_keys=True)
        f.write("\n")

    # Clean up temp TTF if we created one
    if input_type == "sfd" and resolved_path != config.input_path:
        try:
            temp_dir = resolved_path.parent
            resolved_path.unlink(missing_ok=True)
            temp_dir.rmdir()  # only if empty
        except OSError:
            pass

    return report


# ---------------------------------------------------------------------------
# CLI argument parsing
# ---------------------------------------------------------------------------

def build_argparser() -> argparse.ArgumentParser:
    """Build the argument parser."""
    parser = argparse.ArgumentParser(
        description="Convert a vector font into a CP850-indexed 6x8 font ROM.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert Lexis SFD font with defaults
  python3 tools/sfd_to_cp850.py Lexis.sfd \\
      --output-bin build/font.bin \\
      --output-png build/font.png \\
      --output-report build/font.json

  # Convert TTF directly with custom settings
  python3 tools/sfd_to_cp850.py MyFont.ttf \\
      --output-bin build/font.bin \\
      --output-png build/font.png \\
      --output-report build/font.json \\
      --render-mode mono \\
      --control-policy cp437-symbols \\
      --strict

  # With manual overrides
  python3 tools/sfd_to_cp850.py Lexis.sfd \\
      --output-bin build/font.bin \\
      --output-png build/font.png \\
      --output-report build/font.json \\
      --overrides overrides.json \\
      --verbose
""",
    )

    parser.add_argument(
        "input",
        type=Path,
        help="Input font file (.sfd, .ttf, or .otf)",
    )

    parser.add_argument(
        "--output-bin",
        type=Path,
        required=True,
        help="Output ROM binary path (2048 bytes)",
    )
    parser.add_argument(
        "--output-png",
        type=Path,
        required=True,
        help="Output PNG contact sheet path",
    )
    parser.add_argument(
        "--output-report",
        type=Path,
        required=True,
        help="Output JSON conversion report path",
    )
    parser.add_argument(
        "--output-raw-png",
        type=Path,
        default=None,
        help="Optional raw unlabelled atlas PNG path",
    )

    parser.add_argument(
        "--width",
        type=int,
        default=6,
        help="Glyph width in pixels (default: 6)",
    )
    parser.add_argument(
        "--height",
        type=int,
        default=8,
        help="Glyph height in pixels (default: 8)",
    )
    parser.add_argument(
        "--baseline",
        type=int,
        default=7,
        help="Baseline row (0-based from top) (default: 7)",
    )
    parser.add_argument(
        "--oversample",
        type=int,
        default=1,
        help="Oversampling factor (default: 1, use 1 for mono mode)",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=0,
        help="Grayscale threshold 0-255 (default: 0)",
    )
    parser.add_argument(
        "--render-mode",
        type=str,
        default="mono",
        choices=["grayscale", "mono"],
        help="Rendering mode: grayscale or mono (default: mono)",
    )
    parser.add_argument(
        "--control-policy",
        type=str,
        default="cp437-symbols",
        choices=["blank", "ascii-visible", "cp437-symbols"],
        help="How to render control characters 0x00-0x1F, 0x7F (default: cp437-symbols)",
    )
    parser.add_argument(
        "--swatch-scale",
        type=int,
        default=12,
        help="PNG swatch pixel scale (default: 12)",
    )
    parser.add_argument(
        "--x-align",
        type=str,
        default="center",
        choices=["center", "bearing"],
        help="Horizontal alignment mode (default: center)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        default=False,
        help="Exit with error if glyphs are missing, clipped, or ROM size is wrong",
    )
    parser.add_argument(
        "--overrides",
        type=Path,
        default=None,
        help="Optional JSON bitmap override file",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        default=False,
        help="Verbose output to stderr",
    )

    return parser


# ---------------------------------------------------------------------------
# main()
# ---------------------------------------------------------------------------

def main(argv: Optional[Sequence[str]] = None) -> int:
    """Entry point. Returns 0 on success, non-zero on error."""
    parser = build_argparser()
    args = parser.parse_args(argv)

    # Validate input file exists
    if not args.input.exists():
        print(f"Error: input file not found: {args.input}", file=sys.stderr)
        return 1

    suffix = args.input.suffix.lower()
    if suffix not in (".sfd", ".ttf", ".otf"):
        print(
            f"Error: unsupported input format: {suffix}. Expected .sfd, .ttf, or .otf.",
            file=sys.stderr,
        )
        return 1

    # Check fontforge for SFD inputs
    if suffix == ".sfd" and not _find_fontforge():
        print(
            "Error: fontforge executable not found. Install FontForge to convert .sfd files:\n"
            "  Debian/Ubuntu: sudo apt install fontforge\n"
            "  Arch:          sudo pacman -S fontforge\n"
            "  macOS:         brew install fontforge",
            file=sys.stderr,
        )
        return 1

    # Build config
    config = ConverterConfig(
        input_path=args.input.resolve(),
        output_bin=args.output_bin.resolve(),
        output_png=args.output_png.resolve(),
        output_report=args.output_report.resolve(),
        output_raw_png=args.output_raw_png.resolve() if args.output_raw_png else None,
        width=args.width,
        height=args.height,
        baseline=args.baseline,
        oversample=args.oversample,
        threshold=args.threshold,
        render_mode=args.render_mode,
        control_policy=args.control_policy,
        swatch_scale=args.swatch_scale,
        x_align=args.x_align,
        strict=args.strict,
        overrides_path=args.overrides.resolve() if args.overrides else None,
        verbose=args.verbose,
    )

    # Validate config
    validation_errors = config.validate()
    if validation_errors:
        for err in validation_errors:
            print(f"Error: {err}", file=sys.stderr)
        return 1

    # Validate overrides file if specified
    if config.overrides_path:
        try:
            load_overrides(config.overrides_path, config.width)
        except (ValueError, OSError) as e:
            print(f"Error: invalid overrides file: {e}", file=sys.stderr)
            return 1

    # Run conversion
    try:
        report = convert(config)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except RuntimeError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: unexpected failure: {e}", file=sys.stderr)
        if config.verbose:
            import traceback
            traceback.print_exc()
        return 1

    # Print text summary
    print_text_summary(report)

    # Strict check
    if config.strict:
        failures = strict_check(report, config)
        if failures:
            print("\nStrict mode failures:", file=sys.stderr)
            for f in failures:
                print(f"  - {f}", file=sys.stderr)
            return 1

    if config.verbose:
        print(f"\nROM written:    {config.output_bin}", file=sys.stderr)
        print(f"PNG written:    {config.output_png}", file=sys.stderr)
        print(f"Report written: {config.output_report}", file=sys.stderr)
        if config.output_raw_png:
            print(f"Raw atlas:      {config.output_raw_png}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
