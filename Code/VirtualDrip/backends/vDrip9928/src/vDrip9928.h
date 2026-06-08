/*
 * Troy's TMS9918 Emulator - Core interface
 *
 * Copyright (c) 2021 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vDrip9928
 *
 */

#ifndef _VDRIP9928_H_
#define _VDRIP9928_H_

/* ------------------------------------------------------------------
 * LINKAGE MODES:
 * 
 * Default (nothing defined):    When your executable is using vDrip9928 as a DLL
 * VR_6502_EMU_COMPILING_DLL:    When compiling vDrip9928 as a DLL
 * VR_6502_EMU_STATIC:           When linking vrEmu6502 statically in your executable
 */

#if __EMSCRIPTEN__
#include <emscripten.h>
  #ifdef __cplusplus
  #define VR_EMU_TMS9918_DLLEXPORT EMSCRIPTEN_KEEPALIVE extern "C"
  #define VR_EMU_TMS9918_DLLEXPORT_CONST extern "C"
#else
  #define VR_EMU_TMS9918_DLLEXPORT EMSCRIPTEN_KEEPALIVE extern
  #define VR_EMU_TMS9918_DLLEXPORT_CONST extern
#endif
#elif VR_TMS9918_EMU_COMPILING_DLL
#define VR_EMU_TMS9918_DLLEXPORT __declspec(dllexport)
#elif defined WIN32 && !defined VR_EMU_TMS9918_STATIC
#define VR_EMU_TMS9918_DLLEXPORT __declspec(dllimport)
#else
#ifdef __cplusplus
#define VR_EMU_TMS9918_DLLEXPORT extern "C"
#else
#define VR_EMU_TMS9918_DLLEXPORT extern
#endif
#endif

#ifndef VR_EMU_TMS9918_DLLEXPORT_CONST
#define VR_EMU_TMS9918_DLLEXPORT_CONST VR_EMU_TMS9918_DLLEXPORT
#endif

#include <stdint.h>
#include <stdbool.h>

/* PRIVATE DATA STRUCTURE
 * ---------------------------------------- */
struct vDrip9928_s;
typedef struct vDrip9928_s VDrip9928;

typedef enum
{
  TMS_MODE_GRAPHICS_I,
  TMS_MODE_GRAPHICS_II,
  TMS_MODE_TEXT,
  TMS_MODE_MULTICOLOR,
  TMS_MODE_TEXT_2,
} vDrip9928Mode;

typedef enum
{
  TMS_TRANSPARENT = 0,
  TMS_BLACK,
  TMS_MED_GREEN,
  TMS_LT_GREEN,
  TMS_DK_BLUE,
  TMS_LT_BLUE,
  TMS_DK_RED,
  TMS_CYAN,
  TMS_MED_RED,
  TMS_LT_RED,
  TMS_DK_YELLOW,
  TMS_LT_YELLOW,
  TMS_DK_GREEN,
  TMS_MAGENTA,
  TMS_GREY,
  TMS_WHITE,
} vDrip9928Color;

typedef enum
{
  TMS_REG_0 = 0,
  TMS_REG_1,
  TMS_REG_2,
  TMS_REG_3,
  TMS_REG_4,
  TMS_REG_5,
  TMS_REG_6,
  TMS_REG_7,
  TMS_NUM_REGISTERS,
  TMS_REG_NAME_TABLE        = TMS_REG_2,
  TMS_REG_COLOR_TABLE       = TMS_REG_3,
  TMS_REG_PATTERN_TABLE     = TMS_REG_4,
  TMS_REG_SPRITE_ATTR_TABLE = TMS_REG_5,
  TMS_REG_SPRITE_PATT_TABLE = TMS_REG_6,
  TMS_REG_FG_BG_COLOR       = TMS_REG_7,
} vDrip9928Register;

#define TMS9918_PIXELS_X 512
#define TMS9918_PIXELS_Y 192


/* PUBLIC INTERFACE
 * ---------------------------------------- */

 /* Function:  vDrip9928New
  * --------------------
  * create a new TMS9918
  */
VR_EMU_TMS9918_DLLEXPORT
VDrip9928* vDrip9928New(void);

/* Function:  vDrip9928Reset
  * --------------------
  * reset the new TMS9918
  */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928Reset(VDrip9928* tms9918);

/* Function:  vDrip9928Destroy
 * --------------------
 * destroy a TMS9918
 *
 * tms9918: tms9918 object to destroy / clean up
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928Destroy(VDrip9928* tms9918);

/* Function:  vDrip9928WriteAddr
 * --------------------
 * write an address (mode = 1) to the tms9918
 *
 * uint8_t: the data (DB0 -> DB7) to send
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928WriteAddr(VDrip9928* tms9918, uint8_t data);

/* Function:  vDrip9928WriteData
 * --------------------
 * write data (mode = 0) to the tms9918
 *
 * uint8_t: the data (DB0 -> DB7) to send
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928WriteData(VDrip9928* tms9918, uint8_t data);

/* Function:  vDrip9928ReadStatus
 * --------------------
 * read from the status register
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t vDrip9928ReadStatus(VDrip9928* tms9918);

/* Function:  vDrip9928ReadData
 * --------------------
 * read data (mode = 0) from the tms9918
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t vDrip9928ReadData(VDrip9928* tms9918);

/* Function:  vDrip9928ReadDataNoInc
 * --------------------
 * read data (mode = 0) from the tms9918
 * don't increment the address pointer
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t vDrip9928ReadDataNoInc(VDrip9928* tms9918);


/* Function:  vDrip9928ScanLine
 * ----------------------------------------
 * generate a scanline
 *
 * pixels to be filled with TMS9918 color palette indexes (vDrip9928Color)
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928ScanLine(VDrip9928* tms9918, uint8_t y, uint8_t pixels[TMS9918_PIXELS_X]);

/* Function:  vDrip9928RegValue
 * ----------------------------------------
 * return a reigister value
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t vDrip9928RegValue(VDrip9928* tms9918, vDrip9928Register reg);

/* Function:  vDrip9928WriteRegValue
 * ----------------------------------------
 * write a reigister value
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928WriteRegValue(VDrip9928* tms9918, vDrip9928Register reg, uint8_t value);


/* Function:  vDrip9928VramValue
 * ----------------------------------------
 * return a value from vram
 */
VR_EMU_TMS9918_DLLEXPORT
uint8_t vDrip9928VramValue(VDrip9928* tms9918, uint16_t addr);


/* Function:  vDrip9928DisplayEnabled
  * --------------------
  * check BLANK flag
  */
VR_EMU_TMS9918_DLLEXPORT
bool vDrip9928DisplayEnabled(VDrip9928* tms9918);


/* Function:  vDrip9928DisplayMode
  * --------------------
  * current display mode
  */
VR_EMU_TMS9918_DLLEXPORT
vDrip9928Mode vDrip9928DisplayMode(VDrip9928* tms9918);


#endif // _VDRIP9928_H_
