#ifndef VIDEO_DEVICE_VDRIP9958_H
#define VIDEO_DEVICE_VDRIP9958_H

/**
 * @file video_device_vdrip9958.h
 * V9958 VideoDevice adapter — wraps the standalone vDrip9958 emulator.
 */

#include "video_device.h"

/** Create a new V9958 video backend. Returns NULL on allocation failure. */
VideoDevice *video_device_vdrip9958_create(void);

/** Execute one decoded V9958 command-stream operation. */
bool video_device_vdrip9958_stream_op(
    VideoDevice *device,
    uint8_t opcode,
    const uint8_t *operands,
    uint16_t operand_size,
    StreamState *state);

#endif
