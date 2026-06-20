# Code Guide

## Source Layout

```
src/                          — Proxy application
  protocol.h/.c               — Packet types, constants, encoder, stream decoder
  packet_parser.h/.c          — Streaming frame decoder
  packet_dispatch.h/.c        — Central dispatch, reply routing, state ownership
  packet_replay.h/.c          — File replay reader
  serial_port.h/.c            — POSIX serial I/O, framed/raw TX
  serial_reader.h/.c          — Serial reader thread
  keyboard_transport.h/.c     — Keyboard writer thread, traffic gates
  input_keyboard.h/.c         — VNC keysym → terminal byte mapping
  pty_console.h/.c            — PTY console bridge
  storage_protocol.h/.c       — CP/M drive-image protocol
  storage_backend.h/.c        — Flat file image backend
  video_device.h/.c           — Generic backend contract + wrappers
  video_device_tms9928.c      — TMS9918 adapter
  video_device_vdrip9928.c    — vDrip9928 adapter
  video_device_vdrip9958.c    — V9958 adapter
  display_libvncserver.h/.c   — LibVNCServer wrapper
  virtual_text_cursor.h/.c    — Proxy cursor overlay
  app_config.h/.c             — CLI parsing
  app_runtime.h/.c            — Signal handling, shutdown
  main.c                      — Orchestration, backend factory
  test_protocol.c             — Integrated smoke tests

backends/
  vDrip9958/src/              — Standalone emulator library
    vDrip9958.h               — Public API
    vDrip9958_internal.h      — Private state, helpers
    vDrip9958.c               — Core, ports, registers, display decode
    vDrip9958_render.c        — Scanline rendering (all modes)
    vDrip9958_commands.c      — Command engine

tests/
  generators/                 — Python packet generators
  packets/                    — Binary replay fixtures
tools/
  serial_replay.py            — Fixture replay over serial
```

## Key Call Flows

### Packet Reception
```
serial_reader → packet_parser_feed → complete Packet
    → packet_dispatch_handle_packet
        → storage/PTY/keyboard (early exit)
        → FRAME_MARK → frame_mark() → render → stream_state_reset
        → PACKET_RESET → emulator reset + state reset + upload abort
        → COMMAND_STREAM → stream_decode()
        → video_device_handle_packet() → adapter
```

### Reply Transmission
```
adapter handle_packet → VideoDeviceResult.reply_kind
    → dispatch_send_reply()
        → serial_port_send_packet()
```

### Stream Decoding
```
stream_decode(payload, len, &state, &upload, &result, device)
    → for each opcode:
        → lookup descriptor
        → validate operands
        → update StreamState or UploadState
        → mark result.dirty/presentation
```

## Where to Find Definitions

| What | Where |
|---|---|
| Packet types | `src/protocol.h` (enum `PacketType`) |
| Stream opcodes | `src/protocol.h` (enum `StreamOpcode`) |
| Opcode descriptor table | `src/protocol.c` (`opcode_table`) |
| MAX_PACKET_PAYLOAD | `src/protocol.h` (1024) |
| StreamState / UploadState | `src/protocol.h` |
| VideoDevice contract | `src/video_device.h` |
| Reply dispatch | `src/packet_dispatch.c` (`dispatch_send_reply`) |
| Backend factory | `src/main.c` (`create_video_backend`) |
| V9958 public API | `backends/vDrip9958/src/vDrip9958.h` |

---

*Derived from source. Verified 2026-06-20.*
