# vDrip9928 — Reverse Engineering Documentation

> **Project**: vDrip9928 — TMS9918/VDP Emulator for Virtual Drip Console  
> **Upstream**: [vrEmuTms9918](https://github.com/visrealm/vrEmuTms9918) by Troy Schrapel  
> **License**: MIT  
> **Reverse Engineered**: 2026-05-29

---

## What Is This?

vDrip9928 is a local integration of **vrEmuTms9918**, a zero-dependency C99 library that emulates the Texas Instruments **TMS9918A / TMS9928A / TMS9929A Video Display Processor** (VDP). It renders all four documented TMS9918 display modes (Graphics I, Graphics II, Text, Multicolor) with full sprite, collision, and interrupt emulation — scanline by scanline.

The project name "vDrip9928" reflects its role: it is the **TMS9918 rendering backend** for the Zephyr-80 Virtual Drip console system. The Z80 CP/M host sends VDP register state and VRAM over the Virtual Drip serial protocol; this emulator renders the frame on the modern host side.

---

## Documentation Index

| Document | Description |
|----------|-------------|
| [architecture.md](architecture.md) | System architecture and data flow |
| [components.md](components.md) | Component inventory and file manifest |
| [api-reference.md](api-reference.md) | Public API reference |
| [technology-stack.md](technology-stack.md) | Technology stack and build system |
| [dependencies.md](dependencies.md) | Dependency analysis |
| [integration-context.md](integration-context.md) | How vDrip9928 fits into the larger Zephyr-80/Virtual Drip system |

---

## Quick Stats

| Metric | Value |
|--------|-------|
| Language | C99 (core), C++11 (Python bindings) |
| Total source lines | ~1,384 |
| Core library size | ~877 lines (C + headers) |
| Python bindings | ~90 lines (C++ + Python) |
| External dependencies | **None** (core), pybind11 (bindings only) |
| Built artifacts | `libvrEmuTms9918.so`, `libvrEmuTms9918Util.so` |
| Supported display modes | 4 (Graphics I, Graphics II, Text, Multicolor) |
| Resolution | 256×192 pixels, 15-color + transparent palette |
| VRAM | 16 KB emulated |
