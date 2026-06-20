# Limitations

## By Design

- No CRC, checksum, or integrity on the wire
- No capability negotiation or protocol versioning (matched-pair host + proxy)
- Old display-wire compatibility intentionally removed
- Fixed canvas per backend; no dynamic VNC resize
- Single-process proxy; no multi-client VNC support

## Unsupported Features

- Cycle-accurate timing, `/WAIT` states, DRAM refresh modeling
- Analog video output, color burst, external video bus
- Mouse, light pen, external interrupt sources
- Dynamic RFB desktop resizing
- V9958 horizontal scrolling in bitmap modes (coarse/fine registers exist
  but scroll behavior is lightly tested)

## Implemented But Lightly Tested

- G6 interlace rendering (smoke-tested only)
- Sprite mode 2 behavior
- Command engine edge cases (boundary conditions, logical operations)
- YJK/YAE color modes

## Deferred Features

- Text/cell bitmap rendering via G6 command operations (opcodes parse but do
  not produce visible output)
- CPU-transfer command acceleration (CE/TR observation works; all-data
  accelerated form deferred)
- YJK color space rendering
- Full VRAM address tracking after command-engine operations

## Legacy Backend Limitations

- TMS9928 and vDrip9928 backends do not support read replies, palette port,
  or indirect register port
- `VDP_SCROLL` on vDrip9928 is a hardware-specific optimization (Text 2 name
  table shift), not the V9958 logical scroll adapter
- Legacy fixtures must be regenerated for new frame format (CRC removed)

---

*Derived from source review and AIDLC deferred-feature tracking. Verified 2026-06-20.*
