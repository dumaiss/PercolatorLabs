# Documentation Review

**Date**: 2026-06-20

## Files Inspected

- `src/protocol.h` — packet types, constants, stream opcodes, state structs
- `src/protocol.c` — encoder, `packet_type_name()`, stream decoder, state machines
- `src/packet_parser.h/.c` — parser state machine, 6 states, no CRC
- `src/packet_dispatch.h/.c` — routing, replies, FRAME_MARK, cursor policy
- `src/video_device.h/.c` — backend contract, result struct, wrappers
- `src/video_device_vdrip9958.c` — V9958 adapter, 4 ports, canvas, scroll alias
- `backends/vDrip9958/src/vDrip9958.h` — public emulator API
- `backends/vDrip9958/src/vDrip9958_render.c` — mode decode, renderers
- `src/serial_port.h/.c` — send signatures (uint16_t)
- `src/display_libvncserver.c` — VNC setup (reverted resize changes)
- `src/main.c` — backend factory, --test flag
- `src/test_protocol.c` — 6 integrated tests
- `tests/generators/vdrip_packets.py` — new frame format, no CRC
- `tools/serial_replay.py` — 2-byte length parsing
- `CMakeLists.txt` — vDrip9958 subdirectory, link target
- `backends/vDrip9958/src/vDrip9958.c` — register masks, display decode
- `backends/vDrip9958/src/vDrip9958_internal.h` — private state layout

## Tests/Builds Run

- `make` — clean build, all 3 backends + vDrip9958 shared library
- `./virtual-vdp --test` — 6/6 tests pass
- `python3 tools/serial_replay.py --dry-run` — all fixtures parse correctly

## Stale Documentation Corrected

- `README.md` — updated for current backends, new frame format, docs links
- Protocol header comments — updated for no-CRC frame

## Code/Document Discrepancies Found

1. **Stream decoder stubs text/cell rendering**: The functional design specifies
   G6 bitmap rendering via LMMM/LMMV command sequences. The current
   `stream_decode()` implementation marks `framebuffer_dirty = true` for
   text/cell opcodes but does NOT call V9958 command operations. This is
   documented in `docs/acceleration.md` as deferred.

2. **Upload via stream only**: `PACKET_VRAM_UPLOAD_BEGIN/DATA/END` (0x1B-0x1D)
   are defined in `protocol.h` but the proxy only processes uploads through
   `OP_UPLOAD_BEGIN/DATA/END` stream opcodes. The top-level packet types are
   reserved for future use.

3. **VDP_SCROLL on V9958**: The adapter accepts the packet as a legacy alias
   but defers the actual logical scroll to the stream decoder (which also
   defers rendering). Documented in `docs/limitations.md`.

4. **BL bit position**: `src/protocol.h` does not document V9958 register bit
   layout. The emulator's renderer uses R#1D6 for BL. Test generators
   confirmed this is 0x40 = unblanked.

## Remaining Areas Not Verified

- G6 interlace rendering with exhaustive mode combinations
- Command engine boundary conditions
- Sprite mode 2 exact behavior
- YJK/YAE color modes
- CPU-transfer command observation path
- Storage protocol round-trip with actual Z80 host
- Serial RTS/CTS behavior with physical hardware

---

*All documentation claims are verified against source code as of 2026-06-20.*
