/*
 * vDrip9958 - Yamaha V9958 video display processor emulation
 *
 * Public C API.
 *
 * Copyright (c) 2026 Virtual Drip contributors
 *
 * This code is licensed under the MIT license.
 *
 * Derived in part, by permission, from the Virtual Drip vDrip9928 backend,
 * itself a permanent fork of "vrEmuTms9918" by Troy Schrapel
 * (Copyright (c) 2021 Troy Schrapel), used under the MIT license.
 * See LICENSE for the original notices. The V9958 register model, display
 * modes, command engine, palette, and 128 KiB VRAM behavior are new work.
 */

#ifndef VDRIP9958_H
#define VDRIP9958_H

/* ------------------------------------------------------------------
 * Linkage control
 *
 *   (nothing defined)          Consuming vDrip9958 as a shared library.
 *   VDRIP9958_COMPILING_DLL    Compiling vDrip9958 as a Windows DLL.
 *   VDRIP9958_STATIC           Consuming/compiling vDrip9958 statically.
 * ------------------------------------------------------------------ */
#if defined(_WIN32) && !defined(VDRIP9958_STATIC)
  #ifdef VDRIP9958_COMPILING_DLL
    #define VDRIP9958_API __declspec(dllexport)
  #else
    #define VDRIP9958_API __declspec(dllimport)
  #endif
#else
  #define VDRIP9958_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------
 * Opaque instance
 *
 * All emulator state is private. Callers hold only this opaque handle and
 * must serialize all access to a single instance themselves.
 * ------------------------------------------------------------------ */
struct VDrip9958_s;
typedef struct VDrip9958_s VDrip9958;

/* ------------------------------------------------------------------
 * Output geometry bounds
 *
 * A scanline buffer supplied to vDrip9958ScanLine must hold at least
 * VDRIP9958_MAX_WIDTH pixels. Native widths are 256 or 512; interlaced
 * output weaves two fields up to VDRIP9958_MAX_HEIGHT lines.
 * ------------------------------------------------------------------ */
#define VDRIP9958_MAX_WIDTH   512
#define VDRIP9958_MAX_HEIGHT  424

/* ------------------------------------------------------------------
 * Display modes
 *
 * Decoded from the current register state. A reserved or unsupported mode
 * combination decodes to VDRIP9958_MODE_INVALID.
 * ------------------------------------------------------------------ */
typedef enum
{
  VDRIP9958_MODE_TEXT1 = 0,
  VDRIP9958_MODE_TEXT2,
  VDRIP9958_MODE_MULTICOLOR,
  VDRIP9958_MODE_GRAPHIC1,
  VDRIP9958_MODE_GRAPHIC2,
  VDRIP9958_MODE_GRAPHIC3,
  VDRIP9958_MODE_GRAPHIC4,
  VDRIP9958_MODE_GRAPHIC5,
  VDRIP9958_MODE_GRAPHIC6,
  VDRIP9958_MODE_GRAPHIC7,
  VDRIP9958_MODE_INVALID
} VDrip9958Mode;

/* ------------------------------------------------------------------
 * Display metadata snapshot
 *
 * Immutable value describing the current native output geometry. It exposes
 * no internal pointers. When the mode is invalid, geometry/field fields
 * retain the most recent valid values.
 * ------------------------------------------------------------------ */
typedef struct
{
  uint16_t      width;       /* native output width in pixels         */
  uint16_t      height;      /* native output height in lines         */
  VDrip9958Mode mode;        /* decoded display mode                  */
  bool          interlaced;  /* true when interlace is active         */
  uint8_t       field;       /* current field: 0 or 1                 */
} VDrip9958DisplayInfo;


/* ==================================================================
 * Lifecycle
 * ================================================================== */

/* Create a new V9958 instance with its own 128 KiB VRAM, reset to a
 * deterministic power-on state. Returns NULL on allocation failure. */
VDRIP9958_API
VDrip9958* vDrip9958New(void);

/* Reset an instance to the deterministic power-on state. NULL is ignored. */
VDRIP9958_API
void vDrip9958Reset(VDrip9958* vdp);

/* Destroy an instance and release its VRAM. NULL is accepted. */
VDRIP9958_API
void vDrip9958Destroy(VDrip9958* vdp);


/* ==================================================================
 * CPU-visible ports (V9958 I/O ports 0..3)
 * ================================================================== */

/* Port 0 - VRAM data write. NULL is ignored. */
VDRIP9958_API
void vDrip9958WriteData(VDrip9958* vdp, uint8_t value);

/* Port 0 - VRAM data read. Returns 0 on NULL. */
VDRIP9958_API
uint8_t vDrip9958ReadData(VDrip9958* vdp);

/* Port 1 - control write (address setup / direct register write).
 * NULL is ignored. */
VDRIP9958_API
void vDrip9958WriteControl(VDrip9958* vdp, uint8_t value);

/* Port 1 - status read (register selected by R#15). Returns 0 on NULL. */
VDRIP9958_API
uint8_t vDrip9958ReadStatus(VDrip9958* vdp);

/* Port 2 - palette write (two bytes per entry, indexed by R#16).
 * NULL is ignored. */
VDRIP9958_API
void vDrip9958WritePalette(VDrip9958* vdp, uint8_t value);

/* Port 3 - indirect register write (target/auto-increment from R#17).
 * NULL is ignored. */
VDRIP9958_API
void vDrip9958WriteRegisterIndirect(VDrip9958* vdp, uint8_t value);


/* ==================================================================
 * Rendering
 * ================================================================== */

/* Render one native scanline at line 'y' into 'pixels', which must hold at
 * least VDRIP9958_MAX_WIDTH entries. Each pixel is 0x00RRGGBB. NULL instance
 * or NULL buffer is ignored.
 *
 * Unit 1 fills the line with a deterministic border/background placeholder;
 * Unit 2 replaces this with real mode rendering. */
VDRIP9958_API
void vDrip9958ScanLine(VDrip9958* vdp, uint16_t y, uint32_t* pixels);


/* ==================================================================
 * Display metadata
 * ================================================================== */

/* Return the current display metadata snapshot. On NULL, returns a zeroed
 * snapshot with mode VDRIP9958_MODE_INVALID. */
VDRIP9958_API
VDrip9958DisplayInfo vDrip9958GetDisplayInfo(const VDrip9958* vdp);


/* ==================================================================
 * Command engine
 * ================================================================== */

/* Advance an active VRAM command by one logical step. Returns true if a
 * command remains active (CE set) after the step, false otherwise.
 *
 * Unit 1 has no command engine: this always returns false and has no
 * effect. Unit 3 implements real command stepping. */
VDRIP9958_API
bool vDrip9958StepCommand(VDrip9958* vdp);


#ifdef __cplusplus
}
#endif

#endif /* VDRIP9958_H */
