# Virtual Drip — Remote VDP for Zephyr-80

**Part of the *Percolator Pixels* series**

## Overview

Virtual Drip is a software video card for the Zephyr-80 homebrew Z80 computer.
Instead of driving a physical VDP, Zephyr streams VDP operations over a serial
link to a modern PC. A proxy application reconstructs the VDP state, renders
video output, and serves it through a standard VNC client.

Three video backends are selectable at runtime:

| Backend | Chip | Native Size | Canvas |
|---|---|---|---|
| `tms9928` | TMS9918A/9928A | 256×192 | 256×192 |
| `vdrip9928` | Enhanced TMS9918 + Text 2 | 256×192 | 256×192 |
| `vdrip9958` | Yamaha V9958 | 256–512 × 192–424 | 512×424 |

The standalone `vDrip9958` library emulates the full Yamaha V9958: 128 KiB
VRAM, four CPU-visible ports, 10 display modes, programmable palette, sprite
modes 1/2, hardware command engine, and caller-owned RGB scanline output.

## Key Features

- Wire protocol with 1024-byte payloads, 16-bit LE length, no CRC
- Compact command streams, VRAM uploads, text/cell acceleration (V9958)
- CP/M drive-image storage over serial
- PTY console bridge, raw keyboard input
- RTS/CTS hardware flow control
- VNC/RFB presentation via LibVNCServer
- File replay fixtures and integrated smoke tests

## Quick Start

```bash
cd build && cmake .. && make
./virtual-vdp --test
./virtual-vdp --video-backend vdrip9958 tests/packets/v9958_g6_interlace.bin
```

## Documentation

- [Architecture](docs/architecture.md)
- [Wire Protocol](docs/protocol.md)
- [V9958 Emulator](docs/emulator.md)
- [Command Stream](docs/v9958-command-stream.md)
- [Acceleration](docs/acceleration.md)
- [API Reference](docs/api-reference.md)
- [Code Guide](docs/code-guide.md)
- [Backend Guide](docs/backend-guide.md)
- [Host BIOS Guide](docs/host-bios-guide.md)
- [Testing](docs/testing.md)
- [Limitations](docs/limitations.md)

## License

MIT
