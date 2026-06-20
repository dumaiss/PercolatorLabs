# Host BIOS Integration Guide

**This document is integration guidance, not implemented BIOS behavior.**
The Zephyr-80 CP/M BIOS implementing these recommendations is a separate
future project.

## Architecture

The Z80 host should separate:

- **VT-100 / ANSI terminal parser** — local console semantics
- **Semantic console API** — cursor positioning, scrolling, attribute changes
- **Virtual Drip transport** — serial framing and RTS/CTS
- **Backend-specific console driver** — translates semantic operations to
  wire packets

```text
Application (CP/M BDOS/CCP)
    ↓
VT-100 Parser / Console Driver
    ↓
Virtual Drip Transport (serial framing + RTS/CTS)
    ↓
Proxy → V9958 / VNC
```

## Frame Encoder Requirements

The Z80 must encode the new frame format:

```
A5 5A LEN_LO LEN_HI TYPE PAYLOAD...
```

- 16-bit LE declared length = TYPE + PAYLOAD bytes
- No CRC computation needed
- Maximum payload: 1024 bytes

## Preserved Services

Storage and keyboard protocols are unchanged under the new frame. The Z80
storage driver uses the same request/reply sequence format. Keyboard input
arrives as raw terminal bytes (default) or `TERMINAL_RX` packets (PTY mode).

## Console Integration

### Initialization Sequence
1. Open serial port, establish RTS/CTS
2. Wait for `PROXY_READY` from proxy
3. Send `RESET` packet
4. Configure V9958 mode via register writes or command stream
5. Upload font glyphs via VRAM upload
6. Configure accelerator state (`OP_SET_*`)

### Text Output Flow
1. Application writes character to console
2. Console driver adds character to text-run buffer
3. Buffer is flushed when:
   - Buffer full (e.g., 32 characters)
   - Cursor position changes explicitly
   - Attribute changes
   - Explicit flush requested
   - `FRAME_MARK` / presentation needed
4. Flush produces `COMMAND_STREAM` with `OP_TEXT_RUN`

### Scrolling
- Host issues `OP_SCROLL_UP` (full-screen) or `OP_SCROLL_REGION` explicitly
- Text runs do NOT auto-scroll
- `PACKET_VDP_SCROLL` is a legacy alias; new code should use stream opcodes

## Retained State

The proxy remembers: cursor position, colors, VRAM address, screen/glyph
bases, atlas config, display offset. The host sets these once and omits
them from subsequent streams.

After `FRAME_MARK`, retained state resets. The host must reconfigure after
each frame mark, or include explicit state setters in every stream.

## Incremental Integration Order

1. Serial framing (new format) + `PROXY_READY` handshake
2. Storage protocol (unchanged)
3. Raw VDP register writes to enter a visible mode
4. Command stream with basic port writes
5. VRAM upload for font
6. Text-run output
7. Scrolling
8. CPU-transfer commands (deferred)

## Physical vs Virtual V9958

- **Physical V9958**: register writes are immediate; VRAM writes have
  bus-cycle timing; command engine has `/WAIT` states
- **Virtual V9958 (proxy)**: register writes are fire-and-forget serial
  packets; VRAM writes are batched; command engine is stepped remotely

The BIOS driver should isolate these differences behind a hardware abstraction
so the same console code works with a physical card.

---

*Integration guidance only. Not implemented in any current Zephyr-80 BIOS.*
