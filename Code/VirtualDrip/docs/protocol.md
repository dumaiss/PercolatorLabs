# Wire Protocol Specification

## Frame Format

```
Offset  Size  Field
------  ----  -----
  0      1    SYNC0 = 0xA5
  1      1    SYNC1 = 0x5A
  2      1    LEN_LO (declared length, low byte)
  3      1    LEN_HI (declared length, high byte)
  4      1    TYPE (packet type, 0x01..0x1E)
  5..N   *    PAYLOAD (0..1024 bytes)
```

- Declared length is 16-bit little-endian, counts TYPE + PAYLOAD bytes (1..1025)
- Payload length = declared length − 1 (0..1024)
- No CRC, checksum, or integrity field
- Minimum valid frame: 5 bytes (type-only, zero payload)
- Maximum frame: 1029 bytes (SYNC(2) + LEN(2) + TYPE(1) + PAYLOAD(1024))
- Maximum payload: `MAX_PACKET_PAYLOAD = 1024`

## Parser Behavior

Defined in `src/packet_parser.c`. States:

```
WAIT_SYNC0 → WAIT_SYNC1 → READ_LEN_LO → READ_LEN_HI → READ_TYPE → READ_PAYLOAD → dispatch → WAIT_SYNC0
```

- **Sync search**: bytes not matching `A5 5A` are consumed silently
- **Back-to-back A5**: the second `A5` restarts the sync window
- **Invalid declared length** (<1 or >1025): logged to stderr, parser returns to sync search
- **Type-only packet** (declared length = 1): dispatched immediately from `READ_TYPE`
- **Truncated frame** (EOF before complete): detected by `packet_parser_has_partial_packet()`
- **No error counters**: framing errors are logged only

## Packet Type Table

### Legacy Display (TMS9918 compatible)

| ID | Name | Dir | Payload | Reply | Backend |
|---|---|---|---|---|---|
| 0x01 | `VDP_CTRL_WRITE` | Z→P | 1 byte | None | All |
| 0x02 | `VDP_DATA_WRITE` | Z→P | 1 byte | None | All |
| 0x03 | `VDP_STATUS_READ` | Z→P | 0 | None (legacy) | All |
| 0x04 | `VDP_DATA_READ` | Z→P | 0 | None (legacy) | All |
| 0x0B | `VDP_DATA_BLOCK` | Z→P | 1..1024 bytes | None | All |
| 0x0C | `VDP_SCROLL` | Z→P | 1 byte (rows) | None | Legacy value; active BIOS uses `OP_SCROLL_UP` |

### Control

| ID | Name | Dir | Payload | Notes |
|---|---|---|---|---|
| 0x06 | `RESET` | Z→P | 0 | Resets emulator |
| 0x07 | `PING` | Z→P | 0 | No-op |
| 0x08 | `FRAME_MARK` | Z→P | 0 | Triggers render; resets retained accelerator state (V9958) |
| 0x0A | `PROXY_READY` | P→Z | 0 | Startup handshake |

### Storage

| ID | Name | Dir | Payload |
|---|---|---|---|
| 0x0D | `STORAGE_READ_REQ` | Z→P | 6 bytes (seq, drive, LBA LE) |
| 0x0E | `STORAGE_READ_REPLY` | P→Z | 130 bytes (seq, status, 128-byte record) |
| 0x0F | `STORAGE_WRITE_REQ` | Z→P | 134 bytes (seq, drive, LBA LE, 128-byte record) |
| 0x10 | `STORAGE_WRITE_REPLY` | P→Z | 2 bytes (seq, status) |

### Terminal / Keyboard

| ID | Name | Dir | Payload |
|---|---|---|---|
| 0x05 | `TERMINAL_INPUT` | P→Z | 1..N terminal bytes |
| 0x11 | `TERMINAL_TX` | Z→P | 1..N terminal bytes |
| 0x12 | `TERMINAL_RX` | P→Z | 1..N terminal bytes |
| 0x09 | `CURSOR_COMMAND` | Z→P | 1..N (subcommand + operands) |

Keyboard input defaults to raw terminal bytes (unframed), not `TERMINAL_INPUT` packets.
PTY console mode uses `TERMINAL_TX`/`TERMINAL_RX`.

### V9958 Port Operations

| ID | Name | Dir | Payload | Reply |
|---|---|---|---|---|
| 0x13 | `VDP_PALETTE_WRITE` | Z→P | 1 byte | None |
| 0x14 | `VDP_INDIRECT_WRITE` | Z→P | 1 byte | None |
| 0x15 | `VDP_STATUS_READ_REQ` | Z→P | 0 | `VDP_STATUS_REPLY` |
| 0x16 | `VDP_DATA_READ_REQ` | Z→P | 0 | `VDP_DATA_REPLY` |

### V9958 Replies

| ID | Name | Dir | Payload |
|---|---|---|---|
| 0x17 | `VDP_STATUS_REPLY` | P→Z | 1 byte (status) |
| 0x18 | `VDP_DATA_REPLY` | P→Z | 1 byte (data) |
| 0x19 | `PROTOCOL_ERROR` | P→Z | 1 byte (code, 0x00 = generic) |

### V9958 Protocol Operations

| ID | Name | Dir | Payload |
|---|---|---|---|
| 0x1A | `COMMAND_STREAM` | Z→P | Opcode sequence |
| 0x1B | `VRAM_UPLOAD_BEGIN` | Z→P | Not used (upload via stream opcodes) |
| 0x1C | `VRAM_UPLOAD_DATA` | Z→P | Not used |
| 0x1D | `VRAM_UPLOAD_END` | Z→P | Not used |
| 0x1E | `PACKET_RESET` | Z→P | 0 (full reset: emulator + retained state + upload) |

## Fire-and-Forget vs Replies

- All write packets (0x01, 0x02, 0x0B, 0x13, 0x14) are fire-and-forget: no reply
- Status/data read requests (0x15, 0x16) produce exactly one reply
- Malformed read requests produce `PROTOCOL_ERROR`
- `FRAME_MARK` and `PACKET_RESET` produce no reply

## Presentation

- `FRAME_MARK` (0x08): triggers render + VNC notify, then resets retained accelerator state (V9958). Does NOT reset emulator registers, VRAM, palette, or mode.
- `PACKET_RESET` (0x1E): resets emulator to power-on state, clears retained accelerator state, aborts any in-progress upload
- `COMMAND_STREAM` with `OP_PRESENT`: renders immediately, preserves retained state

## Compatibility

- Storage and keyboard packet semantics are preserved from the original protocol
- Old display-wire frames (with CRC) are NOT compatible
- Legacy TMS9918 fixtures must be regenerated for the new frame format

---

*Derived from `src/protocol.h`, `src/packet_parser.c`, `src/packet_dispatch.c`. Verified 2026-06-20.*
