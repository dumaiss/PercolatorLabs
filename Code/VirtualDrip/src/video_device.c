#include "video_device.h"

/*
 * These wrappers keep defensive NULL/optional-op handling in one place. Callers
 * can use the same control flow for all backends while concrete implementations
 * stay small.
 */

void video_device_result_clear(VideoDeviceResult *result)
{
    if (result == NULL) {
        return;
    }

    result->accepted = false;
    result->framebuffer_dirty = false;
    result->presentation_requested = false;
    result->dirty_x = 0;
    result->dirty_y = 0;
    result->dirty_w = 0;
    result->dirty_h = 0;
    result->reply_kind = VIDEO_REPLY_NONE;
    result->reply_value = 0;
}

void video_device_result_mark_full(VideoDevice *device, VideoDeviceResult *result)
{
    if (device == NULL || result == NULL) {
        return;
    }

    result->framebuffer_dirty = true;
    result->dirty_x = 0;
    result->dirty_y = 0;
    result->dirty_w = device->info.width;
    result->dirty_h = device->info.height;
}

bool video_device_reset(VideoDevice *device, VideoDeviceResult *result)
{
    video_device_result_clear(result);
    if (device == NULL || device->ops == NULL || device->ops->reset == NULL) {
        return false;
    }

    return device->ops->reset(device, result);
}

bool video_device_handle_packet(VideoDevice *device, const Packet *packet, VideoDeviceResult *result)
{
    video_device_result_clear(result);
    if (device == NULL || device->ops == NULL || device->ops->handle_packet == NULL) {
        return false;
    }

    return device->ops->handle_packet(device, packet, result);
}

bool video_device_frame_mark(VideoDevice *device, VideoDeviceResult *result)
{
    video_device_result_clear(result);
    if (device == NULL || device->ops == NULL || device->ops->frame_mark == NULL) {
        /* default: presentation-only */
        result->presentation_requested = true;
        return true;
    }

    return device->ops->frame_mark(device, result);
}

bool video_device_render_framebuffer(VideoDevice *device, uint32_t *framebuffer, int width, int height)
{
    if (device == NULL || device->ops == NULL || device->ops->render_framebuffer == NULL) {
        return false;
    }

    return device->ops->render_framebuffer(device, framebuffer, width, height);
}

void video_device_tick_frame(VideoDevice *device, VideoDeviceResult *result)
{
    video_device_result_clear(result);
    if (device == NULL || device->ops == NULL || device->ops->tick_frame == NULL) {
        return;
    }

    device->ops->tick_frame(device, result);
}

bool video_device_is_text_mode(VideoDevice *device)
{
    if (device == NULL || device->ops == NULL || device->ops->is_text_mode == NULL) {
        return false;
    }

    return device->ops->is_text_mode(device);
}

void video_device_destroy(VideoDevice *device)
{
    if (device == NULL) {
        return;
    }

    if (device->ops != NULL && device->ops->destroy != NULL) {
        device->ops->destroy(device);
    }
}
