#!/usr/bin/env python3
"""
Unit tests for sfd_to_cp850.py — font-to-CP850-ROM converter.

These tests cover logic that does NOT require the actual Lexis font.
Integration tests (requiring FontForge or real font files) are marked
with the ``integration`` marker and skipped by default.

Usage:
    pytest tests/test_sfd_to_cp850.py -v
    pytest tests/test_sfd_to_cp850.py -v -m "not integration"
    pytest tests/test_sfd_to_cp850.py -v -m "integration"  # needs fontforge + Lexis
"""

import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from typing import List

import pytest

# Ensure we can import sfd_to_cp850 from the sibling tools/ directory
_THIS_DIR = Path(__file__).resolve().parent
_TOOLS_DIR = _THIS_DIR.parent / "tools"
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

import sfd_to_cp850 as fc


# =========================================================================
# CP850 byte-to-Unicode mappings
# =========================================================================

CP850_UNICODE_CASES = [
    (0x41, 0x0041),   # LATIN CAPITAL LETTER A
    (0x80, 0x00C7),   # LATIN CAPITAL LETTER C WITH CEDILLA
    (0x82, 0x00E9),   # LATIN SMALL LETTER E WITH ACUTE
    (0xB3, 0x2502),   # BOX DRAWINGS LIGHT VERTICAL
    (0xC4, 0x2500),   # BOX DRAWINGS LIGHT HORIZONTAL
    (0xDB, 0x2588),   # FULL BLOCK
    (0xE1, 0x00DF),   # LATIN SMALL LETTER SHARP S
    (0x20, 0x0020),   # SPACE
    (0x30, 0x0030),   # DIGIT ZERO
    (0x7E, 0x007E),   # TILDE
    (0xFF, 0x00A0),   # NO-BREAK SPACE (CP850 0xFF)
]


@pytest.mark.parametrize("slot,expected_unicode", CP850_UNICODE_CASES)
def test_cp850_unicode_mapping(slot: int, expected_unicode: int) -> None:
    """Verify that CP850 byte values map to correct Unicode code points."""
    assert fc.cp850_unicode(slot) == expected_unicode


def test_cp850_unicode_out_of_range() -> None:
    """cp850_unicode rejects values outside 0x00–0xFF."""
    with pytest.raises(ValueError):
        fc.cp850_unicode(-1)
    with pytest.raises(ValueError):
        fc.cp850_unicode(256)


# =========================================================================
# Control byte detection
# =========================================================================

CONTROL_BYTES = [0x00, 0x01, 0x08, 0x0D, 0x1A, 0x1F, 0x7F]
NON_CONTROL_BYTES = [0x20, 0x41, 0x80, 0xA0, 0xDB, 0xFE, 0xFF]


@pytest.mark.parametrize("value", CONTROL_BYTES)
def test_is_control_byte_true(value: int) -> None:
    """0x00–0x1F and 0x7F are control bytes."""
    assert fc.is_control_byte(value) is True


@pytest.mark.parametrize("value", NON_CONTROL_BYTES)
def test_is_control_byte_false(value: int) -> None:
    """0x20 and above (except 0x7F) are not control bytes."""
    assert fc.is_control_byte(value) is False


# =========================================================================
# ASCII-visible control-character mapping
# =========================================================================

ASCII_VISIBLE_CASES = [
    (0x00, 0x0040),   # @
    (0x01, 0x0041),   # A
    (0x0D, 0x004D),   # M
    (0x1A, 0x005A),   # Z
    (0x1B, 0x005B),   # [
    (0x1C, 0x005C),   # backslash
    (0x1D, 0x005D),   # ]
    (0x1E, 0x005E),   # ^
    (0x1F, 0x005F),   # _
    (0x7F, 0x2302),   # ⌂ (HOUSE)
]


@pytest.mark.parametrize("slot,expected", ASCII_VISIBLE_CASES)
def test_ascii_visible_mapping(slot: int, expected: int) -> None:
    """Control bytes map to visible ASCII range under ascii-visible policy."""
    result = fc.control_unicode(slot, "ascii-visible")
    assert result == expected


def test_ascii_visible_non_control_raises() -> None:
    """control_unicode raises for non-control bytes."""
    with pytest.raises(ValueError):
        fc.control_unicode(0x41, "ascii-visible")


# =========================================================================
# CP437-symbols control-character mapping
# =========================================================================

CP437_SYMBOL_CASES = [
    (0x01, 0x263A),   # ☺
    (0x02, 0x263B),   # ☻
    (0x03, 0x2665),   # ♥
    (0x04, 0x2666),   # ♦
    (0x05, 0x2663),   # ♣
    (0x06, 0x2660),   # ♠
    (0x0E, 0x266B),   # ♫
    (0x0F, 0x263C),   # ☼
    (0x18, 0x2191),   # ↑
    (0x19, 0x2193),   # ↓
    (0x1A, 0x2192),   # →
    (0x1B, 0x2190),   # ←
    (0x1E, 0x25B2),   # ▲
    (0x1F, 0x25BC),   # ▼
]


@pytest.mark.parametrize("slot,expected", CP437_SYMBOL_CASES)
def test_cp437_symbol_mapping(slot: int, expected: int) -> None:
    """Control bytes map to CP437 symbols under cp437-symbols policy."""
    result = fc.control_unicode(slot, "cp437-symbols")
    assert result == expected


def test_blank_policy_returns_none() -> None:
    """Blank policy returns None for all control bytes."""
    for slot in range(0x20):
        assert fc.control_unicode(slot, "blank") is None
    assert fc.control_unicode(0x7F, "blank") is None


def test_unknown_control_policy_raises() -> None:
    """Unknown control policy raises ValueError."""
    with pytest.raises(ValueError):
        fc.control_unicode(0x01, "invalid-policy")


# =========================================================================
# target_unicode
# =========================================================================

def test_target_unicode_printable() -> None:
    """Printable bytes use CP850 Unicode."""
    assert fc.target_unicode(0x41, "blank") == 0x0041
    assert fc.target_unicode(0xDB, "cp437-symbols") == 0x2588


def test_target_unicode_control_blank() -> None:
    """Control bytes with blank policy return None."""
    assert fc.target_unicode(0x00, "blank") is None
    assert fc.target_unicode(0x7F, "blank") is None


def test_target_unicode_control_ascii_visible() -> None:
    """Control bytes with ascii-visible policy map correctly."""
    assert fc.target_unicode(0x1A, "ascii-visible") == 0x005A


# =========================================================================
# Row packing
# =========================================================================

PACKING_CASES = [
    # (pixels, expected_byte)
    ([True, False, True, True, False, True], 0b10110100),
    ([True, True, True, True, True, True], 0b11111100),
    ([False, False, False, False, False, False], 0b00000000),
    ([True, False, False, False, False, False], 0b10000000),
    ([False, False, False, False, False, True], 0b00000100),
    ([True] * 8, 0b11111111),  # 8-pixel test
]


@pytest.mark.parametrize("pixels,expected", PACKING_CASES)
def test_pack_row_msb_left(pixels: List[bool], expected: int) -> None:
    """Pixels are packed MSB-left, unused bits are zero."""
    width = len(pixels)
    result = fc.pack_row_msb_left(pixels, width)
    assert result == expected, f"Got {result:#010b}, expected {expected:#010b}"


def test_pack_row_low_bits_zero() -> None:
    """Bits 1 and 0 are always zero for 6-pixel width."""
    for _ in range(100):
        # Test with all combinations of 6 pixels (deterministic sample)
        pass
    # Deterministic tests:
    assert (fc.pack_row_msb_left([True] * 6, 6) & 0b00000011) == 0
    assert (fc.pack_row_msb_left([False] * 6, 6) & 0b00000011) == 0
    assert (fc.pack_row_msb_left([True, False, True, False, True, False], 6) & 0b00000011) == 0


def test_pack_row_too_short_raises() -> None:
    """Providing fewer pixels than width raises ValueError."""
    with pytest.raises(ValueError):
        fc.pack_row_msb_left([True], 6)


def test_pack_row_bad_width_raises() -> None:
    """Invalid width raises ValueError."""
    with pytest.raises(ValueError):
        fc.pack_row_msb_left([True] * 6, 0)
    with pytest.raises(ValueError):
        fc.pack_row_msb_left([True] * 8, 9)


def test_pack_row_msb_left_from_int() -> None:
    """pack_row_msb_left_from_int masks low bits correctly."""
    assert fc.pack_row_msb_left_from_int(0b10110111, 6) == 0b10110100
    assert fc.pack_row_msb_left_from_int(0b11111111, 6) == 0b11111100
    assert fc.pack_row_msb_left_from_int(0b00000011, 6) == 0b00000000


# =========================================================================
# ROM construction
# =========================================================================

def test_rom_size_is_2048() -> None:
    """ROM must be exactly 2048 bytes."""
    glyphs: dict = {}
    overrides: dict = {}
    rom = fc.build_rom(glyphs, overrides, 6)
    assert len(rom) == 2048


def test_rom_glyph_order() -> None:
    """Glyph slots are in CP850 byte order: 0x00 bytes 0-7, 0x01 bytes 8-15, ..."""
    glyphs: dict = {}
    overrides: dict = {}

    # Set glyph 0x00 row 0 to all pixels on
    glyphs[0x00] = [[True] * 6 for _ in range(8)]
    # Set glyph 0xFF row 7 to all pixels on
    glyphs[0xFF] = [[True] * 6 for _ in range(8)]

    rom = fc.build_rom(glyphs, overrides, 6)
    assert len(rom) == 2048

    # Glyph 0x00 row 0 should be 0xFC
    assert rom[0] == 0xFC  # 0b11111100
    # Glyph 0x00 row 7 should be 0xFC
    assert rom[7] == 0xFC
    # Glyph 0xFF row 0 is at offset 0xFF * 8 = 2040
    assert rom[2040] == 0xFC
    # Glyph 0xFF row 7 is at offset 2047
    assert rom[2047] == 0xFC


def test_rom_row_order() -> None:
    """Rows within a glyph are ordered 0..7 (top to bottom)."""
    glyphs: dict = {}
    overrides: dict = {}

    # Fill glyph 0x41 with a diagonal pattern
    glyphs[0x41] = []
    for row_idx in range(8):
        row = [False] * 6
        row[min(row_idx % 6, 5)] = True  # diagonal
        glyphs[0x41].append(row)

    rom = fc.build_rom(glyphs, overrides, 6)
    base = 0x41 * 8
    for row_idx in range(8):
        expected = 1 << (7 - min(row_idx % 6, 5))
        assert rom[base + row_idx] == expected, f"Row {row_idx}: got {rom[base + row_idx]:#010b}, expected {expected:#010b}"


def test_rom_overrides_replace_glyphs() -> None:
    """Overrides replace rasterized glyphs in the ROM."""
    glyphs: dict = {}
    overrides = {
        0x41: [0xFC, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00],
    }

    rom = fc.build_rom(glyphs, overrides, 6)
    base = 0x41 * 8
    expected = [0xFC, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00]
    for i, val in enumerate(expected):
        assert rom[base + i] == val, f"Row {i}: expected {val:#04x}, got {rom[base + i]:#04x}"


def test_rom_unset_slots_are_zero() -> None:
    """Unset slots produce zero-filled rows."""
    glyphs: dict = {}
    overrides: dict = {}
    rom = fc.build_rom(glyphs, overrides, 6)
    assert rom == b"\x00" * 2048


# =========================================================================
# Override file parsing
# =========================================================================

VALID_OVERRIDE_JSON = """{
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
}"""


def test_load_overrides_valid() -> None:
    """Valid override JSON parses correctly."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(VALID_OVERRIDE_JSON)
        f.flush()
        overrides_path = Path(f.name)

    try:
        overrides = fc.load_overrides(overrides_path, 6)
        assert 0x41 in overrides
        assert 0xB3 in overrides
        assert overrides[0x41] == [0x30, 0x48, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00]
        assert overrides[0xB3] == [0x20] * 8
    finally:
        overrides_path.unlink()


def test_load_overrides_slot_out_of_range() -> None:
    """Override slot > 0xFF raises ValueError."""
    bad_json = '{"0x100": ["0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00"]}'
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(bad_json)
        f.flush()
        overrides_path = Path(f.name)

    try:
        with pytest.raises(ValueError, match="out of range"):
            fc.load_overrides(overrides_path, 6)
    finally:
        overrides_path.unlink()


def test_load_overrides_wrong_row_count() -> None:
    """Override with != 8 rows raises ValueError."""
    bad_json = '{"0x41": ["0x00", "0x00"]}'
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(bad_json)
        f.flush()
        overrides_path = Path(f.name)

    try:
        with pytest.raises(ValueError, match="exactly 8 rows"):
            fc.load_overrides(overrides_path, 6)
    finally:
        overrides_path.unlink()


def test_load_overrides_low_bits_nonzero() -> None:
    """Override row with non-zero bits in positions 1..0 raises ValueError for width=6."""
    # 0x03 has bits 1 and 0 set
    bad_json = '{"0x41": ["0x03", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00"]}'
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(bad_json)
        f.flush()
        overrides_path = Path(f.name)

    try:
        with pytest.raises(ValueError, match="non-zero bits"):
            fc.load_overrides(overrides_path, 6)
    finally:
        overrides_path.unlink()


def test_load_overrides_row_value_out_of_range() -> None:
    """Override row value > 0xFF raises ValueError."""
    bad_json = '{"0x41": ["0x100", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00"]}'
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(bad_json)
        f.flush()
        overrides_path = Path(f.name)

    try:
        with pytest.raises(ValueError, match="out of range"):
            fc.load_overrides(overrides_path, 6)
    finally:
        overrides_path.unlink()


def test_load_overrides_invalid_json() -> None:
    """Malformed JSON raises ValueError."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write("not json")
        f.flush()
        overrides_path = Path(f.name)

    try:
        with pytest.raises(ValueError, match="Invalid JSON"):
            fc.load_overrides(overrides_path, 6)
    finally:
        overrides_path.unlink()


def test_load_overrides_not_a_dict() -> None:
    """JSON array instead of object raises ValueError."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write("[]")
        f.flush()
        overrides_path = Path(f.name)

    try:
        with pytest.raises(ValueError, match="JSON object"):
            fc.load_overrides(overrides_path, 6)
    finally:
        overrides_path.unlink()


# =========================================================================
# Duplicate bitmap detection
# =========================================================================

def test_detect_duplicates_empty_rom() -> None:
    """An all-zero ROM: printable slots (0x20-0x7E, 0x80-0xFF) are identical.
    Control slots (0x00-0x1F, 0x7F) are excluded by blank policy."""
    rom = b"\x00" * 2048
    dups = fc.detect_duplicates(rom, "blank")
    # All printable slots share the all-zero bitmap → one large duplicate group
    assert len(dups) == 1
    # Should contain all printable slots (256 - 33 control = 223)
    assert len(dups[0]) == 223
    # Control slots should NOT appear in the duplicate group
    for slot_hex in dups[0]:
        slot = int(slot_hex, 16)
        assert not fc.is_control_byte(slot), f"control byte {slot_hex} should not be in duplicates"


def test_detect_duplicates_with_non_blank_policy() -> None:
    """With cp437-symbols policy, all-zero control slots ARE detected as duplicates."""
    rom = b"\x00" * 2048
    dups = fc.detect_duplicates(rom, "cp437-symbols")
    # All 256 slots are identical, so we expect one big group
    assert len(dups) == 1
    assert len(dups[0]) == 256


def test_detect_duplicates_distinct_glyphs() -> None:
    """Distinct glyphs produce no duplicate groups.

    Each glyph encodes its 8-bit slot number across 8 rows, 1 bit per row,
    placed in bit 7 (the MSB).  This guarantees 256 unique 8-byte sequences
    while respecting width=6 (bits 1-0 are zero).
    """
    rom = bytearray(2048)
    for slot in range(256):
        base = slot * 8
        for row in range(8):
            # Each row: bit 'row' of slot → bit 7 of row byte
            if slot & (1 << row):
                rom[base + row] = 0x80
            else:
                rom[base + row] = 0x00
    dups = fc.detect_duplicates(bytes(rom), "blank")
    assert dups == []


def test_detect_duplicates_some_same() -> None:
    """Two identical glyphs are detected as duplicates among otherwise unique glyphs."""
    rom = bytearray(2048)
    # Fill all slots with unique values first (1 bit per row encoding)
    for slot in range(256):
        base = slot * 8
        for row in range(8):
            if slot & (1 << row):
                rom[base + row] = 0x80
            else:
                rom[base + row] = 0x00
    # Now make slot 0x41 and 0x42 identical
    glyph_a = [0xFC, 0x84, 0x84, 0xFC, 0x84, 0x84, 0x84, 0x00]
    for i, val in enumerate(glyph_a):
        rom[0x41 * 8 + i] = val
        rom[0x42 * 8 + i] = val

    dups = fc.detect_duplicates(bytes(rom), "blank")
    # Should find exactly one duplicate group: {0x41, 0x42}
    assert len(dups) == 1
    assert "0x41" in dups[0]
    assert "0x42" in dups[0]

    dups = fc.detect_duplicates(bytes(rom), "blank")
    assert len(dups) == 1
    assert "0x41" in dups[0]
    assert "0x42" in dups[0]


# =========================================================================
# ConverterConfig validation
# =========================================================================

def test_config_valid_defaults() -> None:
    """Default config values pass validation."""
    config = fc.ConverterConfig(
        input_path=Path("/tmp/test.ttf"),
        output_bin=Path("/tmp/out.bin"),
        output_png=Path("/tmp/out.png"),
        output_report=Path("/tmp/out.json"),
        output_raw_png=None,
        width=6, height=8, baseline=7,
        oversample=1, threshold=0,
        render_mode="mono",
        control_policy="cp437-symbols",
        swatch_scale=12,
        x_align="center",
        strict=False,
        overrides_path=None,
        verbose=False,
    )
    assert config.validate() == []


def test_config_invalid_width() -> None:
    """Width out of range raises validation error."""
    config = fc.ConverterConfig(
        input_path=Path("/tmp/test.ttf"),
        output_bin=Path("/tmp/out.bin"),
        output_png=Path("/tmp/out.png"),
        output_report=Path("/tmp/out.json"),
        output_raw_png=None,
        width=0, height=8, baseline=7,
        oversample=4, threshold=128,
        render_mode="grayscale",
        control_policy="cp437-symbols",
        swatch_scale=12,
        x_align="center",
        strict=False,
        overrides_path=None,
        verbose=False,
    )
    errors = config.validate()
    assert len(errors) >= 1
    assert any("width" in e.lower() for e in errors)


def test_config_invalid_render_mode() -> None:
    """Invalid render_mode raises validation error."""
    config = fc.ConverterConfig(
        input_path=Path("/tmp/test.ttf"),
        output_bin=Path("/tmp/out.bin"),
        output_png=Path("/tmp/out.png"),
        output_report=Path("/tmp/out.json"),
        output_raw_png=None,
        width=6, height=8, baseline=7,
        oversample=4, threshold=128,
        render_mode="invalid",
        control_policy="cp437-symbols",
        swatch_scale=12,
        x_align="center",
        strict=False,
        overrides_path=None,
        verbose=False,
    )
    errors = config.validate()
    assert len(errors) >= 1


def test_config_mono_requires_oversample_1() -> None:
    """Mono render mode requires oversample=1."""
    config = fc.ConverterConfig(
        input_path=Path("/tmp/test.ttf"),
        output_bin=Path("/tmp/out.bin"),
        output_png=Path("/tmp/out.png"),
        output_report=Path("/tmp/out.json"),
        output_raw_png=None,
        width=6, height=8, baseline=7,
        oversample=4, threshold=128,
        render_mode="mono",
        control_policy="cp437-symbols",
        swatch_scale=12,
        x_align="center",
        strict=False,
        overrides_path=None,
        verbose=False,
    )
    errors = config.validate()
    assert len(errors) >= 1
    assert any("oversample" in e.lower() for e in errors)


# =========================================================================
# is_control_byte edge cases
# =========================================================================

def test_is_control_byte_0x00() -> None:
    assert fc.is_control_byte(0x00) is True


def test_is_control_byte_0x1F() -> None:
    assert fc.is_control_byte(0x1F) is True


def test_is_control_byte_0x7F() -> None:
    assert fc.is_control_byte(0x7F) is True


def test_is_control_byte_0x20() -> None:
    assert fc.is_control_byte(0x20) is False


def test_is_control_byte_0xFF() -> None:
    assert fc.is_control_byte(0xFF) is False


# =========================================================================
# int_to_pixels
# =========================================================================

def test_int_to_pixels() -> None:
    """int_to_pixels correctly unpacks a row byte."""
    pixels = fc.int_to_pixels(0b10110100, 6)
    expected = [True, False, True, True, False, True]
    assert pixels == expected


def test_int_to_pixels_all_zeros() -> None:
    pixels = fc.int_to_pixels(0x00, 6)
    assert pixels == [False] * 6


def test_int_to_pixels_all_ones_6bit() -> None:
    pixels = fc.int_to_pixels(0xFC, 6)
    assert pixels == [True] * 6


# =========================================================================
# ConversionReport
# =========================================================================

def test_conversion_report_defaults() -> None:
    """ConversionReport has sensible defaults."""
    report = fc.ConversionReport()
    d = report.to_dict()
    assert d["width"] == 6
    assert d["height"] == 8
    assert d["rom_size"] == 2048
    assert d["missing"] == []
    assert d["empty"] == []
    assert d["clipped"] == {"left": [], "right": [], "top": [], "bottom": []}
    assert d["overridden"] == []
    assert d["duplicate_bitmaps"] == []


def test_conversion_report_serializable() -> None:
    """ConversionReport serializes to valid JSON."""
    report = fc.ConversionReport(
        input_font="test.ttf",
        input_type="ttf",
        font_family="Test",
        font_style="Regular",
        missing=["0x00", "0x7F"],
        empty=["0x20"],
        overridden=["0x41"],
        duplicate_bitmaps=[["0x00", "0x7F"]],
        rom_size=2048,
    )
    d = report.to_dict()
    text = json.dumps(d, indent=2, sort_keys=True)
    parsed = json.loads(text)
    assert parsed["input_font"] == "test.ttf"
    assert parsed["input_type"] == "ttf"
    assert "0x00" in parsed["missing"]
    assert len(parsed["overridden"]) == 1


# =========================================================================
# CP850 full coverage check
# =========================================================================

def test_all_cp850_slots_decode() -> None:
    """Every byte 0x00–0xFF decodes to a valid CP850 character."""
    for slot in range(256):
        char = bytes([slot]).decode("cp850")
        assert len(char) == 1
        codepoint = ord(char)
        assert isinstance(codepoint, int)
        assert 0 <= codepoint <= 0xFFFF


# =========================================================================
# ClippedFlags
# =========================================================================

def test_clipped_flags_defaults() -> None:
    """Default ClippedFlags has no clips."""
    cf = fc.ClippedFlags()
    assert cf.left is False
    assert cf.right is False
    assert cf.top is False
    assert cf.bottom is False
    assert cf.any_clipped() is False


def test_clipped_flags_left() -> None:
    cf = fc.ClippedFlags(left=True)
    assert cf.any_clipped() is True


def test_clipped_flags_multiple() -> None:
    cf = fc.ClippedFlags(left=True, top=True)
    assert cf.any_clipped() is True


# =========================================================================
# build_argparser
# =========================================================================

def test_argparser_parses_minimal_args() -> None:
    """Parser accepts minimal required arguments."""
    parser = fc.build_argparser()
    args = parser.parse_args([
        "test.ttf",
        "--output-bin", "out.bin",
        "--output-png", "out.png",
        "--output-report", "out.json",
    ])
    assert str(args.input) == "test.ttf"
    assert str(args.output_bin) == "out.bin"
    assert args.width == 6
    assert args.height == 8
    assert args.render_mode == "mono"


def test_argparser_parses_all_options() -> None:
    """Parser accepts all options."""
    parser = fc.build_argparser()
    args = parser.parse_args([
        "test.sfd",
        "--output-bin", "build/font.bin",
        "--output-png", "build/font.png",
        "--output-report", "build/font.json",
        "--output-raw-png", "build/raw.png",
        "--width", "8",
        "--height", "16",
        "--baseline", "14",
        "--oversample", "2",
        "--threshold", "200",
        "--render-mode", "mono",
        "--control-policy", "blank",
        "--swatch-scale", "8",
        "--x-align", "bearing",
        "--strict",
        "--overrides", "overrides.json",
        "--verbose",
    ])
    assert args.width == 8
    assert args.height == 16
    assert args.baseline == 14
    assert args.oversample == 2
    assert args.threshold == 200
    assert args.render_mode == "mono"
    assert args.control_policy == "blank"
    assert args.swatch_scale == 8
    assert args.x_align == "bearing"
    assert args.strict is True
    assert str(args.overrides) == "overrides.json"
    assert args.verbose is True
    assert str(args.output_raw_png) == "build/raw.png"


# =========================================================================
# Integration tests (require FontForge + actual font)
# =========================================================================


@pytest.mark.integration
def test_integration_ttf_to_rom() -> None:
    """Integration test: convert Lexis TTF to ROM and verify outputs."""
    lexis_ttf = _THIS_DIR.parent / "src" / "Lexis-Regular.ttf"
    if not lexis_ttf.exists():
        pytest.skip(f"Lexis TTF not found at {lexis_ttf}")

    with tempfile.TemporaryDirectory() as tmpdir:
        out_dir = Path(tmpdir)
        bin_path = out_dir / "font.bin"
        png_path = out_dir / "font.png"
        report_path = out_dir / "report.json"

        exit_code = fc.main([
            str(lexis_ttf),
            "--output-bin", str(bin_path),
            "--output-png", str(png_path),
            "--output-report", str(report_path),
            "--width", "6",
            "--height", "8",
            "--baseline", "7",
            "--oversample", "4",
            "--threshold", "128",
            "--render-mode", "grayscale",
            "--control-policy", "cp437-symbols",
            "--verbose",
        ])
        assert exit_code == 0

        # Verify ROM
        assert bin_path.exists()
        rom = bin_path.read_bytes()
        assert len(rom) == 2048

        # Verify PNG
        assert png_path.exists()
        assert png_path.stat().st_size > 0

        # Verify report
        assert report_path.exists()
        report = json.loads(report_path.read_text())
        assert report["rom_size"] == 2048
        assert report["input_type"] == "ttf"


@pytest.mark.integration
def test_integration_sfd_to_rom() -> None:
    """Integration test: convert Lexis SFD to ROM via FontForge."""
    lexis_sfd = _THIS_DIR.parent / "src" / "Lexis-Regular.sfd"
    if not lexis_sfd.exists():
        pytest.skip(f"Lexis SFD not found at {lexis_sfd}")

    # Check fontforge availability
    if not fc._find_fontforge():
        pytest.skip("fontforge not available")

    with tempfile.TemporaryDirectory() as tmpdir:
        out_dir = Path(tmpdir)
        bin_path = out_dir / "font.bin"
        png_path = out_dir / "font.png"
        report_path = out_dir / "report.json"

        exit_code = fc.main([
            str(lexis_sfd),
            "--output-bin", str(bin_path),
            "--output-png", str(png_path),
            "--output-report", str(report_path),
            "--width", "6",
            "--height", "8",
            "--baseline", "7",
            "--oversample", "4",
            "--threshold", "128",
            "--render-mode", "grayscale",
            "--control-policy", "cp437-symbols",
            "--verbose",
        ])
        assert exit_code == 0

        # Verify ROM
        assert bin_path.exists()
        rom = bin_path.read_bytes()
        assert len(rom) == 2048

        # Verify PNG
        assert png_path.exists()

        # Verify report
        assert report_path.exists()
        report = json.loads(report_path.read_text())
        assert report["rom_size"] == 2048
        assert report["input_type"] == "sfd"
