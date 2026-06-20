# Public C API Reference — vDrip9958

Full header: `backends/vDrip9958/src/vDrip9958.h`

## Types

### `VDrip9958` (opaque)
All emulator state is private. Created by `vDrip9958New()`, destroyed by `vDrip9958Destroy()`.

### `VDrip9958Mode` (enum)
`VDRIP9958_MODE_TEXT1`..`VDRIP9958_MODE_GRAPHIC7`, `VDRIP9958_MODE_INVALID`

### `VDrip9958DisplayInfo` (struct)
```c
typedef struct {
    uint16_t      width;       // 256 or 512
    uint16_t      height;      // 192/212 or 384/424 (interlaced)
    VDrip9958Mode mode;
    bool          interlaced;
    uint8_t       field;       // 0 or 1
} VDrip9958DisplayInfo;
```

### Constants
`VDRIP9958_MAX_WIDTH` = 512, `VDRIP9958_MAX_HEIGHT` = 424

## Lifecycle

```c
VDrip9958* vDrip9958New(void);         // NULL on allocation failure
void vDrip9958Reset(VDrip9958* vdp);   // NULL-safe
void vDrip9958Destroy(VDrip9958* vdp); // NULL-safe
```

## Ports

```c
void    vDrip9958WriteData(VDrip9958*, uint8_t value);
uint8_t vDrip9958ReadData(VDrip9958*);           // 0 on NULL
void    vDrip9958WriteControl(VDrip9958*, uint8_t value);
uint8_t vDrip9958ReadStatus(VDrip9958*);          // 0 on NULL
void    vDrip9958WritePalette(VDrip9958*, uint8_t value);
void    vDrip9958WriteRegisterIndirect(VDrip9958*, uint8_t value);
```

Palette writes index from R#16 and auto-increment. Two bytes per entry.

## Rendering

```c
void vDrip9958ScanLine(VDrip9958* vdp, uint16_t y, uint32_t* pixels);
// pixels must hold VDRIP9958_MAX_WIDTH entries
// each pixel is 0x00RRGGBB
// NULL instance or buffer → no-op
```

## Display Metadata

```c
VDrip9958DisplayInfo vDrip9958GetDisplayInfo(const VDrip9958* vdp);
// NULL → zeroed struct with MODE_INVALID
```

## Command Engine

```c
bool vDrip9958StepCommand(VDrip9958* vdp);
// Returns true if command remains active (CE set) after step
```

## Example: Create, Configure, Render

```c
VDrip9958* vdp = vDrip9958New();
// Set Graphic 1, unblanked
vDrip9958WriteControl(vdp, 0x00);             // R#0
vDrip9958WriteControl(vdp, 0x80 | 0);
vDrip9958WriteControl(vdp, 0x40);             // R#1 BL=1
vDrip9958WriteControl(vdp, 0x80 | 1);
// ... write VRAM data via data port ...

// Render scanline 0
uint32_t line[512];
vDrip9958ScanLine(vdp, 0, line);

// Query mode
VDrip9958DisplayInfo info = vDrip9958GetDisplayInfo(vdp);
printf("Mode %d, %dx%d\n", info.mode, info.width, info.height);

vDrip9958Destroy(vdp);
```

## Serialization

Callers must serialize all access to a single `VDrip9958` instance. The
library provides no internal locking.

---

*Derived from `backends/vDrip9958/src/vDrip9958.h`. Verified 2026-06-20.*
