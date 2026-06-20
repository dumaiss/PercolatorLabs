# V9958 Command Stream Protocol

The command stream (`PACKET_COMMAND_STREAM`, 0x1A) carries a sequence of
fixed-format opcodes as the packet payload. The payload length terminates
the stream.

## Stream Parser

Defined in `src/protocol.c:stream_decode()`. Rules:

1. Walk payload byte by byte. Each byte is an opcode.
2. `OpcodeDescriptor` table maps opcode → `{known, variable, fixed_size}`
3. Unknown opcode → stop stream, prior operations preserved
4. Truncated opcode → stop stream, prior operations preserved
5. Known opcode with invalid operands → skip that opcode, continue
6. Variable-length opcodes compute size from first operands
7. No END opcode required; no error counters; errors logged to stderr

## Opcode Table

### Port Writes (1 operand byte)

| Opcode | Name | Operands |
|---|---|---|
| 0x01 | `OP_DATA_WRITE` | value |
| 0x02 | `OP_CTRL_WRITE` | value |
| 0x03 | `OP_PALETTE_WRITE` | value |
| 0x04 | `OP_INDIRECT_WRITE` | value |

### VRAM Operations

| Opcode | Name | Operands |
|---|---|---|
| 0x10 | `OP_REG_BLOCK` | start_reg, count, values[count] |
| 0x11 | `OP_VRAM_ADDR_WRITE` | addr_lo, addr_mid, addr_hi, count, data[count] |
| 0x12 | `OP_VRAM_SEQ_WRITE` | count_lo, count_hi, data[count] |

### Command Setup (15 bytes)

| Opcode | Name | Operands |
|---|---|---|
| 0x20 | `OP_COMMAND_SETUP` | SX(2), SY(2), DX(2), DY(2), NX(2), NY(2), CLR, ARG, CMD |

All operands little-endian. R#32–R#45 written first, R#46 (CMD) last.

### Text/Cell Operations

| Opcode | Name | Operands |
|---|---|---|
| 0x30 | `OP_TEXT_RUN` | flags, col, row, [fg,bg,attr if flags&1], count, chars[count] |
| 0x31 | `OP_CELL_FILL` | col, row, w, h, char_code |
| 0x32 | `OP_CELL_COPY` | sc, sr, dc, dr, w, h |
| 0x33 | `OP_INSERT_LINES` | row, count, top, bottom |
| 0x34 | `OP_DELETE_LINES` | row, count, top, bottom |
| 0x35 | `OP_ERASE_EOL` | col, row |
| 0x36 | `OP_CLEAR_SCREEN` | (none) |
| 0x37 | `OP_SCROLL_REGION` | top, bottom, signed_count |
| 0x38 | `OP_SCROLL_UP` | rows |

### Retained State Setters

| Opcode | Name | Operands |
|---|---|---|
| 0x40 | `OP_SET_CURSOR` | col, row |
| 0x41 | `OP_SET_ATTR` | fg, bg, flags (bit0=reverse) |
| 0x42 | `OP_SET_VRAM_ADDR` | addr_lo, addr_mid, addr_hi |
| 0x43 | `OP_SET_SCREEN_BASE` | addr_lo, addr_mid, addr_hi |
| 0x44 | `OP_SET_GLYPH_BASE` | addr_lo, addr_mid, addr_hi |
| 0x46 | `OP_SET_ATLAS_CONFIG` | atlas_cols |
| 0x47 | `OP_SET_DISP_OFFSET` | display_offset |

Addresses are 3-byte LE (17-bit V9958 VRAM space).

### Upload State Machine

| Opcode | Name | Operands |
|---|---|---|
| 0x50 | `OP_UPLOAD_BEGIN` | addr(3), total(3), flags (must be 0) |
| 0x51 | `OP_UPLOAD_DATA` | offset(3), length(2), data[length] |
| 0x52 | `OP_UPLOAD_END` | (none) |

### Stream Control

| Opcode | Name | Operands |
|---|---|---|
| 0xFE | `OP_PRESENT` | (none) — request immediate render, preserve retained state |
| 0xFF | `OP_NOP` | (none) — no operation |

## Hex Examples

### Low-level operations in one stream
```
A5 5A 0E 00 1A  01 42  02 07  41 0F 00 00  FE
```
- `01 42`: `OP_DATA_WRITE` 0x42
- `02 07`: `OP_CTRL_WRITE` 0x07
- `41 0F 00 00`: `OP_SET_ATTR` fg=15, bg=0, no-reverse
- `FE`: `OP_PRESENT` — render now

Frame: declared length = 14 (0x000E), type = 0x1A, 13 payload bytes.

### VRAM upload
```
A5 5A 1D 00 1A  50 00 10 00 03 00 00 00  51 00 00 00 03 00 41 42 43  51 03 00 00 03 00 44 45 46  52
```
- `50 ...`: `OP_UPLOAD_BEGIN` addr=0x1000, total=3
- `51 ...`: `OP_UPLOAD_DATA` offset=0, len=3, "ABC"
- `51 ...`: `OP_UPLOAD_DATA` offset=3, len=3, "DEF"
- `52`: `OP_UPLOAD_END`

### Truncated stream
```
A5 5A 07 00 1A  01 42  FE  40 28
```
- `01 42`: `OP_DATA_WRITE` 0x42 — applied
- `FE`: `OP_PRESENT` — applied
- `40 28`: `OP_SET_CURSOR` — truncated (needs 2 bytes, only 1 available)
  Stream stops. Prior ops preserved. Presentation flag is set.

---

*Derived from `src/protocol.c:stream_decode()`, `src/protocol.h` opcode table. Verified 2026-06-20.*
