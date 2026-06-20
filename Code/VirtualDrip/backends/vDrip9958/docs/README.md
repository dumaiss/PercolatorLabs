# vDrip9958 Documentation

Index of the maintained documentation for the standalone Yamaha V9958 emulator.

## Topics

- [api-reference.md](api-reference.md) — public types, ports, scanline,
  display-info, and command-step contracts.
- [architecture.md](architecture.md) — core/render/command components, state
  and VRAM lifetime, and workflow boundaries.
- [register-support.md](register-support.md) — control/status/palette register
  support matrix.
- [display-mode-support.md](display-mode-support.md) — display mode, color,
  sprite, scroll, and interlace support matrix.
- [command-support.md](command-support.md) — VRAM command support matrix.
- [building.md](building.md) — GCC/Clang build and CTest commands.
- [testing.md](testing.md) — smoke-test scope and how to extend it.
- [deviations.md](deviations.md) — unsupported functions and deterministic
  choices.
- [verification.md](verification.md) — recorded build/test/static-check
  evidence and remaining manual-validation risk.

## Authoritative specifications

- `yamaha_v9938.pdf` — Yamaha V9938 Technical Data Book.
- `yamaha_v9958_ocr.pdf` — Yamaha V9958 Technical Data Book (OCR).

These manuals are the correctness reference for non-obvious behavior.
