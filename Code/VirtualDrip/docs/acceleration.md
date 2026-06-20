# V9958 Acceleration

Text and cell operations are optional GRAPHIC 6 acceleration macros layered
over the ordinary V9958 emulator. They are NOT the base device model.

## Base Device Invariant

The standalone `vDrip9958` instance is the authoritative base device. Creation
and `PACKET_RESET` leave it in V9958 reset state. Accelerator initialization
must not select a display mode, clear VRAM, upload a font, or alter
hardware-visible state.

Text/cell opcodes execute only when `screen_configured`, `glyph_configured`,
and `atlas_configured` are all true AND the current V9958 mode is
`VDRIP9958_MODE_GRAPHIC6`.

`FRAME_MARK` renders the current V9958 framebuffer, then clears retained
accelerator metadata. It does NOT reset registers, VRAM, palette, or mode.

## Retained Proxy State

```c
typedef struct {
    uint8_t  cursor_col, cursor_row;   // 0..79, 0..23
    uint8_t  foreground, background;   // color indices 0..15
    bool     reverse;
    bool     cursor_wrap_pending;      // after writing column 79 on last row
    uint32_t vram_address;             // 17-bit CPU-port address
    bool     vram_addr_pending;        // set by OP_SET_VRAM_ADDR, applied on next use
    bool     screen_configured;        // OP_SET_SCREEN_BASE set
    bool     glyph_configured;         // OP_SET_GLYPH_BASE set
    bool     atlas_configured;         // OP_SET_ATLAS_CONFIG set
    uint32_t screen_base;              // logical cell buffer (3 bytes/cell)
    uint32_t glyph_base;               // glyph atlas VRAM base
    uint8_t  atlas_cols;              // glyphs per atlas row
    uint8_t  display_offset;           // R#23 value (multiple of 8)
} StreamState;
```

At `FRAME_MARK`: render first, then reset all metadata fields to defaults
(cursor 0,0; fg 15; bg 0; no reverse; all configure flags false; bases 0).

At `PACKET_RESET`: emulator reset + full state reset + upload abort.

## Upload (Generic VRAM Transfer)

Not inherently a font upload. `OP_UPLOAD_BEGIN/DATA/END` transfer arbitrary
data to VRAM at a 17-bit address with exact-duplicate detection via a shadow
buffer. Flags byte must be 0x00.

Atlas configuration is separate: upload the glyph data, then call
`OP_SET_GLYPH_BASE` and `OP_SET_ATLAS_CONFIG`.

## Text Grid (80×24, 6×8)

- Logical cell buffer: 3 bytes/cell (char_code, fg, bg|reverse_flag)
- GRAPHIC 6 bitmap rendering (deferred in current implementation)
- Glyph atlas: G6 packed pixels, 256 bytes/scanline, values 0 and F only
- Atlas origin: `atlas_y = glyph_base / 256`, `atlas_x = (glyph_base % 256) * 2`

## Operations Summary

| Operation | Description |
|---|---|
| Text run | Write chars at absolute position; advance cursor |
| Cell fill | Fill rectangle with char + current attr |
| Cell copy | Copy rectangle with overlap-safe semantics |
| Insert/delete lines | Shift within scroll region |
| Erase EOL | Clear to end of row |
| Clear screen | Fill entire grid with space |
| Scroll region | Scroll arbitrary rows |
| Scroll up | Legacy full-screen alias |

## Presentation vs FRAME_MARK

| Event | Render | Reset Retained State | Reset Emulator |
|---|---|---|---|
| `OP_PRESENT` | Yes | No | No |
| `FRAME_MARK` (0x08) | Yes | Yes (metadata only) | No |
| `PACKET_RESET` (0x1E) | Yes (after reset) | Yes | Yes |

## Current Implementation Status

The stream decoder (`src/protocol.c:stream_decode()`) parses all opcodes,
manages retained state, and runs the upload state machine. Text/cell bitmap
rendering via G6 command operations (LMMM IMP → LMMV AND → LMMV XOR) is
specified in the functional design but **deferred** in the current code.
The opcode execution stubs mark `framebuffer_dirty = true` but do not yet
call V9958 command operations to render glyphs.

---

*Derived from `src/protocol.c:stream_decode()`, `src/protocol.h` opcode table. Verified 2026-06-20.*
