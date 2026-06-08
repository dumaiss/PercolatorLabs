/*
 * Troy's TMS9918 Emulator - Utility / helper functions
 *
 * Copyright (c) 2022 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/vDrip9928
 *
 */

#ifndef _VDRIP9928_UTIL_H_
#define _VDRIP9928_UTIL_H_

#include "vDrip9928.h"

#include <stddef.h>
#include <string.h>

#define TMS_R0_MODE_GRAPHICS_I    0x00
#define TMS_R0_MODE_GRAPHICS_II   0x02
#define TMS_R0_MODE_MULTICOLOR    0x00
#define TMS_R0_MODE_TEXT          0x00
#define TMS_R0_EXT_VDP_ENABLE     0x01
#define TMS_R0_EXT_VDP_DISABLE    0x00

#define TMS_R1_RAM_16K            0x80
#define TMS_R1_RAM_4K             0x00
#define TMS_R1_DISP_BLANK         0x00
#define TMS_R1_DISP_ACTIVE        0x40
#define TMS_R1_INT_ENABLE         0x20
#define TMS_R1_INT_DISABLE        0x00
#define TMS_R1_MODE_GRAPHICS_I    0x00
#define TMS_R1_MODE_GRAPHICS_II   0x00
#define TMS_R1_MODE_MULTICOLOR    0x08
#define TMS_R1_MODE_TEXT          0x10
#define TMS_R1_MODE_TEXT_2        0x18
#define TMS_R1_SPRITE_8           0x00
#define TMS_R1_SPRITE_16          0x02
#define TMS_R1_SPRITE_MAG1        0x00
#define TMS_R1_SPRITE_MAG2        0x01

#define TMS_DEFAULT_VRAM_NAME_ADDRESS          0x3800
#define TMS_DEFAULT_VRAM_COLOR_ADDRESS         0x0000
#define TMS_DEFAULT_VRAM_PATT_ADDRESS          0x2000
#define TMS_DEFAULT_VRAM_SPRITE_ATTR_ADDRESS   0x3B00
#define TMS_DEFAULT_VRAM_SPRITE_PATT_ADDRESS   0x1800

 /*
  * TMS9918 palette (RGBA)
  */
VR_EMU_TMS9918_DLLEXPORT_CONST uint32_t vDrip9928Palette[];

/*
 * Write a register value
 */
inline static void vDrip9928WriteRegisterValue(VDrip9928* tms9918, vDrip9928Register reg, uint8_t value)
{
  vDrip9928WriteAddr(tms9918, value);
  vDrip9928WriteAddr(tms9918, 0x80 | (uint8_t)reg);
}

/*
 * Set current VRAM address for reading
 */
inline static void vDrip9928SetAddressRead(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928WriteAddr(tms9918, addr & 0x00ff);
  vDrip9928WriteAddr(tms9918, ((addr & 0xff00) >> 8));
}

/*
 * Set current VRAM address for writing
 */
inline static void vDrip9928SetAddressWrite(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928SetAddressRead(tms9918, addr | 0x4000);
}

/*
 * Write a series of bytes to the VRAM
 */
inline static void vDrip9928WriteBytes(VDrip9928* tms9918, const uint8_t* bytes, size_t numBytes)
{
  for (size_t i = 0; i < numBytes; ++i)
  {
    vDrip9928WriteData(tms9918, bytes[i]);
  }
}

/*
 * Write a series of bytes to the VRAM
 */
inline static void vDrip9928WriteByteRpt(VDrip9928* tms9918, uint8_t byte, size_t rpt)
{
  for (size_t i = 0; i < rpt; ++i)
  {
    vDrip9928WriteData(tms9918, byte);
  }
}


/*
 * Write a series of chars to the VRAM
 */
inline static void vDrip9928WriteString(VDrip9928* tms9918, const char* str)
{
  size_t len = strlen(str);
  for (size_t i = 0; i < len; ++i)
  {
    vDrip9928WriteData(tms9918, str[i]);
  }
}

/*
 * Write a series of chars to the VRAM with offset
 */
inline static void vDrip9928WriteStringOffset(VDrip9928* tms9918, const char* str, uint8_t offset)
{
  size_t len = strlen(str);
  for (size_t i = 0; i < len; ++i)
  {
    vDrip9928WriteData(tms9918, str[i] + offset);
  }
}

/*
 * Return a colur byte consisting of foreground and background colors
 */
inline static uint8_t vDrip9928FgBgColor(vDrip9928Color fg, vDrip9928Color bg)
{
  return (uint8_t)((uint8_t)fg << 4) | (uint8_t)bg;
}

/*
 * Set name table address
 */
inline static void vDrip9928SetNameTableAddr(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928WriteRegisterValue(tms9918, TMS_REG_NAME_TABLE, addr >> 10);
}

/*
 * Set color table address
 */
inline static void vDrip9928SetColorTableAddr(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928WriteRegisterValue(tms9918, TMS_REG_COLOR_TABLE, (uint8_t)(addr >> 6));
}

/*
 * Set pattern table address
 */
inline static void vDrip9928SetPatternTableAddr(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928WriteRegisterValue(tms9918, TMS_REG_PATTERN_TABLE, addr >> 11);
}

/*
 * Set sprite attribute table address
 */
inline static void vDrip9928SetSpriteAttrTableAddr(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928WriteRegisterValue(tms9918, TMS_REG_SPRITE_ATTR_TABLE, (uint8_t)(addr >> 7));
}

/*
 * Set sprite pattern table address
 */
inline static void vDrip9928SetSpritePattTableAddr(VDrip9928* tms9918, uint16_t addr)
{
  vDrip9928WriteRegisterValue(tms9918, TMS_REG_SPRITE_PATT_TABLE, addr >> 11);
}

/*
 * Set foreground (text mode) and background colors
 */
inline static void vDrip9928SetFgBgColor(VDrip9928* tms9918, vDrip9928Color fg, vDrip9928Color bg)
{
  vDrip9928WriteRegisterValue(tms9918, TMS_REG_FG_BG_COLOR, vDrip9928FgBgColor(fg, bg));
}


/*
 * Initialise for Graphics I mode
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928InitialiseGfxI(VDrip9928* tms9918);

/*
 * Initialise for Graphics II mode
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928InitialiseGfxII(VDrip9928* tms9918);

/*
 * Initialise for Text 2 mode (80 columns, 480x192)
 */
VR_EMU_TMS9918_DLLEXPORT
void vDrip9928InitialiseText2(VDrip9928* tms9918);

#endif // _VDRIP9928_UTIL_H_
