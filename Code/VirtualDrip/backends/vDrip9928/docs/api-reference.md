# API Reference

## Core API (`vrEmuTms9918.h`)

All functions use `VR_EMU_TMS9918_DLLEXPORT` linkage (resolves to `extern "C"` or `extern` depending on platform).

### Lifecycle

```c
VrEmuTms9918* vrEmuTms9918New(void);
```
Allocates and initializes a new TMS9918 emulator instance. Returns NULL on allocation failure.

```c
void vrEmuTms9918Reset(VrEmuTms9918* tms9918);
```
Resets an existing instance: clears registers, status, address state, read-ahead buffer. VRAM is NOT cleared (left in unknown state, matching real hardware).

```c
void vrEmuTms9918Destroy(VrEmuTms9918* tms9918);
```
Frees the emulator instance.

### VDP Port Emulation

```c
void vrEmuTms9918WriteAddr(VrEmuTms9918* tms9918, uint8_t data);
```
Emulates the two-stage write to the VDP control port:
- **Stage 0**: Stores the byte as register value or address LSB
- **Stage 1**: If bit 7 set → writes to register `data & 0x07`. If bit 7 clear → sets address pointer (bit 6 = write mode, bits 5–0 = address MSB)

```c
void vrEmuTms9918WriteData(VrEmuTms9918* tms9918, uint8_t data);
```
Writes a byte to VRAM at the current address, then auto-increments the address. Resets the write stage counter. Also updates the read-ahead buffer.

```c
uint8_t vrEmuTms9918ReadStatus(VrEmuTms9918* tms9918);
```
Reads and **clears** the status register. Resets write stage counter.

```c
uint8_t vrEmuTms9918ReadData(VrEmuTms9918* tms9918);
```
Returns the read-ahead buffer contents, reads the next VRAM byte into the buffer, auto-increments address. Resets write stage.

```c
uint8_t vrEmuTms9918ReadDataNoInc(VrEmuTms9918* tms9918);
```
Returns the read-ahead buffer without advancing the address pointer.

### Direct Register Access

```c
uint8_t vrEmuTms9918RegValue(VrEmuTms9918* tms9918, vrEmuTms9918Register reg);
void    vrEmuTms9918WriteRegValue(VrEmuTms9918* tms9918, vrEmuTms9918Register reg, uint8_t value);
```
Direct read/write of VDP registers (bypassing the two-stage protocol). Writing triggers mode re-detection.

### Direct VRAM Access

```c
uint8_t vrEmuTms9918VramValue(VrEmuTms9918* tms9918, uint16_t addr);
```
Reads a single byte from VRAM at the given address (masked to 14 bits).

### Scanline Rendering

```c
void vrEmuTms9918ScanLine(VrEmuTms9918* tms9918, uint8_t y, uint8_t pixels[TMS9918_PIXELS_X]);
```
Renders scanline `y` (0–191) into `pixels`. Output values are `vrEmuTms9918Color` palette indices (0–15). If display is blanked or y ≥ 192, fills with background color. Sets the VSYNC interrupt flag on the last scanline (y=191) if interrupts are enabled.

### Query

```c
bool vrEmuTms9918DisplayEnabled(VrEmuTms9918* tms9918);
```
Returns true if the BLANK bit (R1 bit 6) is clear.

```c
vrEmuTms9918Mode vrEmuTms9918DisplayMode(VrEmuTms9918* tms9918);
```
Returns the current display mode enum.

---

## Utility API (`vrEmuTms9918Util.h`)

All utility functions are `inline static` (header-only) except for the initializers and palette.

### VRAM Helpers

```c
void vrEmuTms9918SetAddressRead(VrEmuTms9918* tms9918, uint16_t addr);
void vrEmuTms9918SetAddressWrite(VrEmuTms9918* tms9918, uint16_t addr);
```
Set the VRAM address pointer for reading or writing. `SetAddressWrite` calls `SetAddressRead` with bit 14 (`0x4000`) set.

```c
void vrEmuTms9918WriteBytes(VrEmuTms9918* tms9918, const uint8_t* bytes, size_t numBytes);
void vrEmuTms9918WriteByteRpt(VrEmuTms9918* tms9918, uint8_t byte, size_t rpt);
void vrEmuTms9918WriteString(VrEmuTms9918* tms9918, const char* str);
void vrEmuTms9918WriteStringOffset(VrEmuTms9918* tms9918, const char* str, uint8_t offset);
```
Bulk write operations to VRAM. Strings are written as ASCII values; `WriteStringOffset` adds an offset to each character.

### Register Helpers

```c
void vrEmuTms9918WriteRegisterValue(VrEmuTms9918* tms9918, vrEmuTms9918Register reg, uint8_t value);
```
Atomic register write via the two-stage protocol.

```c
void vrEmuTms9918SetNameTableAddr(VrEmuTms9918* tms9918, uint16_t addr);     // R2: addr >> 10
void vrEmuTms9918SetColorTableAddr(VrEmuTms9918* tms9918, uint16_t addr);    // R3: addr >> 6
void vrEmuTms9918SetPatternTableAddr(VrEmuTms9918* tms9918, uint16_t addr);   // R4: addr >> 11
void vrEmuTms9918SetSpriteAttrTableAddr(VrEmuTms9918* tms9918, uint16_t addr); // R5: addr >> 7
void vrEmuTms9918SetSpritePattTableAddr(VrEmuTms9918* tms9918, uint16_t addr); // R6: addr >> 11
```
Convenience setters that shift addresses into the register-appropriate format.

### Color

```c
uint8_t vrEmuTms9918FgBgColor(vrEmuTms9918Color fg, vrEmuTms9918Color bg);
void    vrEmuTms9918SetFgBgColor(VrEmuTms9918* tms9918, vrEmuTms9918Color fg, vrEmuTms9918Color bg);
```
Pack FG/BG colors into one byte (FG in upper nibble, BG in lower nibble) and write to R7.

### Palette

```c
extern const uint32_t vrEmuTms9918Palette[16];
```
16-entry RGBA palette. Index 0 is transparent (`0x00000000`). Indexes 1–15 correspond to TMS9918 colors. Each entry is a 32-bit `0xRRGGBBAA` value.

### Mode Initializers

```c
void vrEmuTms9918InitialiseGfxI(VrEmuTms9918* tms9918);
void vrEmuTms9918InitialiseGfxII(VrEmuTms9918* tms9918);
```
Full initialization sequence: write all registers, set table addresses, clear VRAM, initialize sprite attributes.

---

## Python Bindings API

```python
from tms9918 import Tms9918

t = Tms9918()                        # Create emulator instance
t.setReg(reg: int, val: int)         # Write one register (0–7)
t.setRegs(vals: list[int])           # Write all registers at once
t.setVram(addr: int, data: bytes)    # Write bytes to VRAM at address
rgb = t.getScreen()                  # Render to RGB bytearray (256*192*3 bytes)
```

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `TMS9918_PIXELS_X` | 256 | Screen width |
| `TMS9918_PIXELS_Y` | 192 | Screen height |
| `VRAM_SIZE` | 16384 | 16 KB VRAM |
| `MAX_SPRITES` | 32 | Maximum sprites |
| `MAX_SCANLINE_SPRITES` | 4 | Sprites per scanline limit |
| `LAST_SPRITE_YPOS` | 0xD0 | Sentinel Y value for end of sprite list |

## Enums

### `vrEmuTms9918Mode`
```c
TMS_MODE_GRAPHICS_I    // Graphics I (Mode 1)
TMS_MODE_GRAPHICS_II   // Graphics II (Mode 2 / "Bitmap")
TMS_MODE_TEXT          // Text mode (40×24)
TMS_MODE_MULTICOLOR    // Multicolor mode (64×48)
```

### `vrEmuTms9918Color`
```c
TMS_TRANSPARENT = 0,  TMS_BLACK,     TMS_MED_GREEN,  TMS_LT_GREEN,
TMS_DK_BLUE,          TMS_LT_BLUE,   TMS_DK_RED,     TMS_CYAN,
TMS_MED_RED,          TMS_LT_RED,    TMS_DK_YELLOW,  TMS_LT_YELLOW,
TMS_DK_GREEN,         TMS_MAGENTA,   TMS_GREY,       TMS_WHITE
```

### `vrEmuTms9918Register`
```c
TMS_REG_0  .. TMS_REG_7     // Raw register numbers (0–7)
TMS_REG_NAME_TABLE           // Alias for R2
TMS_REG_COLOR_TABLE          // Alias for R3
TMS_REG_PATTERN_TABLE        // Alias for R4
TMS_REG_SPRITE_ATTR_TABLE    // Alias for R5
TMS_REG_SPRITE_PATT_TABLE    // Alias for R6
TMS_REG_FG_BG_COLOR          // Alias for R7
TMS_NUM_REGISTERS            // = 8
```
