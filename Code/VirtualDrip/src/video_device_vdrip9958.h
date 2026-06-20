#ifndef VIDEO_DEVICE_VDRIP9958_H
#define VIDEO_DEVICE_VDRIP9958_H

/**
 * @file video_device_vdrip9958.h
 * V9958 VideoDevice adapter — wraps the standalone vDrip9958 emulator.
 */

#include "video_device.h"

/** Create a new V9958 video backend. Returns NULL on allocation failure. */
VideoDevice *video_device_vdrip9958_create(void);

#endif
