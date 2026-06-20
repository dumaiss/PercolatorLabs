#ifndef VIDEO_DEVICE_H
#define VIDEO_DEVICE_H

/**
 * @file video_device.h
 * Stable interface between Virtual Drip core and video-chip backends.
 *
 * A VideoDevice is a chip/card personality behind a small C interface. The
 * transport, replay, display, and keyboard code talk to VideoDevice; they do
 * not include vrEmuTms9918 headers or know about future chips. Backends receive
 * Virtual Drip packets, decide which ones they understand, update their own
 * state, and render into a host-owned framebuffer.
 *
 * Backends must not call LibVNCServer or serial APIs directly. Display updates
 * are reported through VideoDeviceUpdate so the core can render and mark VNC
 * dirty rectangles. This is the extension point for future V9958/RX660/etc.
 * backends without rewriting transport or display code.
 */

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct VideoDevice VideoDevice;

/** Static capabilities and dimensions advertised by a backend instance. */
typedef struct {
    const char *name;
    int width;
    int height;
    bool supports_status_read;
    bool supports_data_read;
    bool supports_palette_port;
    bool supports_indirect_port;
    bool allows_proxy_cursor_overlay;
} VideoDeviceInfo;

/**
 * Kind of reply a video operation may produce.
 */
typedef enum {
    VIDEO_REPLY_NONE,
    VIDEO_REPLY_STATUS_BYTE,
    VIDEO_REPLY_DATA_BYTE,
    VIDEO_REPLY_PROTOCOL_ERROR
} VideoReplyKind;

/**
 * Result of a backend operation.
 *
 * Carries both dirty-rectangle and optional reply semantics. The dispatch
 * layer uses reply_kind to decide whether to send a serial reply.
 */
typedef struct {
    bool accepted;
    bool framebuffer_dirty;
    bool presentation_requested;
    int dirty_x;
    int dirty_y;
    int dirty_w;
    int dirty_h;
    VideoReplyKind reply_kind;
    uint8_t reply_value;
} VideoDeviceResult;

/**
 * Backend vtable.
 *
 * reset, handle_packet, render_framebuffer, and destroy are expected for normal
 * devices. tick_frame is optional for devices that need frame-time progression.
 * frame_mark is optional; missing implementations default to presentation-only.
 * Missing operations are treated as no-ops or failures by wrapper functions.
 */
typedef struct {
    bool (*reset)(VideoDevice *device, VideoDeviceResult *result);
    bool (*handle_packet)(VideoDevice *device, const Packet *packet, VideoDeviceResult *result);
    bool (*frame_mark)(VideoDevice *device, VideoDeviceResult *result);
    bool (*render_framebuffer)(VideoDevice *device, uint32_t *framebuffer, int width, int height);
    void (*tick_frame)(VideoDevice *device, VideoDeviceResult *result);
    bool (*is_text_mode)(VideoDevice *device);
    void (*destroy)(VideoDevice *device);
} VideoDeviceOps;

/**
 * Public backend handle.
 *
 * ops points to static backend operations. impl is private backend-owned state.
 * The concrete create function owns allocation and video_device_destroy() owns
 * release through ops->destroy().
 */
struct VideoDevice {
    const VideoDeviceOps *ops;
    void *impl;
    VideoDeviceInfo info;
};

/** Clear a result structure to defaults. */
void video_device_result_clear(VideoDeviceResult *result);

/** Mark the whole backend framebuffer as dirty in the result. */
void video_device_result_mark_full(VideoDevice *device, VideoDeviceResult *result);

/** Reset backend state to power-on/default state. */
bool video_device_reset(VideoDevice *device, VideoDeviceResult *result);

/** Dispatch one protocol packet to the backend. */
bool video_device_handle_packet(VideoDevice *device, const Packet *packet, VideoDeviceResult *result);

/** Handle FRAME_MARK: present and reset retained state. */
bool video_device_frame_mark(VideoDevice *device, VideoDeviceResult *result);

/**
 * Render current backend state into a caller-owned framebuffer.
 *
 * Rendering should be idempotent: calling it repeatedly without intervening
 * backend state changes should produce the same pixels.
 */
bool video_device_render_framebuffer(VideoDevice *device, uint32_t *framebuffer, int width, int height);

/** Advance one frame for backends that need time-based updates. */
void video_device_tick_frame(VideoDevice *device, VideoDeviceResult *result);

/** Return true when the current backend display mode is text. */
bool video_device_is_text_mode(VideoDevice *device);

/** Destroy a backend instance created by a concrete factory. */
void video_device_destroy(VideoDevice *device);

#endif
