#include "video_device_vdrip9958.h"

#include "vDrip9958.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * V9958 concrete video backend. Owns one VDrip9958 instance and maps
 * Virtual Drip packets to the four V9958 CPU-visible port roles. Non-
 * CPU-transfer commands are stepped to completion after control writes.
 * Rendering uses a fixed 512x424 canvas with centered native output.
 */

/* ------------------------------------------------------------------
 * Private adapter state
 * ------------------------------------------------------------------ */
typedef struct {
    VDrip9958 *vdp;
    size_t control_writes;
    size_t data_writes;
    size_t palette_writes;
    size_t indirect_writes;
    size_t status_reads;
    size_t data_reads;
} Vdrip9958Device;

#define VDRIP9958_CANVAS_WIDTH   512
#define VDRIP9958_CANVAS_HEIGHT  424
#define VDRIP9958_CLEAR_COLOR    0x00000000u

/* ------------------------------------------------------------------
 * Command classification
 *
 * Non-CPU-transfer commands are auto-completed after a control write.
 * CPU-transfer commands (LMCM, LMMV CPU-transfer variants) are left
 * active for the host to drive via CE/TR or all-data acceleration.
 *
 * Command codes derived from Yamaha V9958 documentation and the
 * vDrip9958 emulator's internal command implementation.
 * ------------------------------------------------------------------ */
typedef enum {
    V9958_CMD_CLASS_NONE,
    V9958_CMD_CLASS_NON_CPU,
    V9958_CMD_CLASS_CPU_TRANSFER,
} V9958CommandClass;

static V9958CommandClass classify_command(uint8_t command_byte)
{
    uint8_t code = command_byte & 0xF0;
    switch (code) {
    case 0x00: return V9958_CMD_CLASS_NON_CPU;  /* STOP     */
    case 0x50: return V9958_CMD_CLASS_NON_CPU;  /* POINT    */
    case 0x60: return V9958_CMD_CLASS_NON_CPU;  /* PSET     */
    case 0x70: return V9958_CMD_CLASS_NON_CPU;  /* LINE     */
    case 0x80: return V9958_CMD_CLASS_NON_CPU;  /* SRCH     */
    case 0x90: return V9958_CMD_CLASS_NON_CPU;  /* LMMV     */
    case 0xA0: return V9958_CMD_CLASS_NON_CPU;  /* LMMM     */
    case 0xB0: return V9958_CMD_CLASS_NON_CPU;  /* LMMC     */
    case 0xC0: return V9958_CMD_CLASS_NON_CPU;  /* HMMV     */
    case 0xD0: return V9958_CMD_CLASS_NON_CPU;  /* HMMM     */
    case 0xE0: return V9958_CMD_CLASS_NON_CPU;  /* YMMM     */
    case 0xF0: return V9958_CMD_CLASS_NON_CPU;  /* HMMC     */
    default:   return V9958_CMD_CLASS_NONE;
    }
}

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */
static Vdrip9958Device *vdrip9958_impl(VideoDevice *device)
{
    return (Vdrip9958Device *)device->impl;
}

static void step_non_cpu_command(Vdrip9958Device *impl)
{
    while (vDrip9958StepCommand(impl->vdp)) {
        /* step to completion */
    }
}

/* ------------------------------------------------------------------
 * VideoDeviceOps implementation
 * ------------------------------------------------------------------ */
static bool vdrip9958_reset(VideoDevice *device, VideoDeviceResult *result)
{
    Vdrip9958Device *impl = vdrip9958_impl(device);

    if (impl->vdp != NULL) {
        vDrip9958Destroy(impl->vdp);
    }

    impl->vdp = vDrip9958New();
    if (impl->vdp == NULL) {
        fprintf(stderr, "Failed to create vDrip9958 instance\n");
        result->accepted = false;
        return false;
    }

    impl->control_writes = 0;
    impl->data_writes = 0;
    impl->palette_writes = 0;
    impl->indirect_writes = 0;
    impl->status_reads = 0;
    impl->data_reads = 0;

    result->accepted = true;
    result->framebuffer_dirty = true;
    return true;
}

static bool vdrip9958_handle_packet(VideoDevice *device, const Packet *packet,
                                     VideoDeviceResult *result)
{
    Vdrip9958Device *impl = vdrip9958_impl(device);
    result->accepted = false;
    result->reply_kind = VIDEO_REPLY_NONE;

    switch (packet->type) {

    /* --- fire-and-forget writes ------------------------------------ */
    case PACKET_VDP_DATA_WRITE:
        if (packet->length != 1) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        ++impl->data_writes;
        vDrip9958WriteData(impl->vdp, packet->payload[0]);
        result->accepted = true;
        result->framebuffer_dirty = true;
        return true;

    case PACKET_VDP_CTRL_WRITE:
        if (packet->length != 1) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        ++impl->control_writes;
        vDrip9958WriteControl(impl->vdp, packet->payload[0]);
        step_non_cpu_command(impl);
        result->accepted = true;
        result->framebuffer_dirty = true;
        return true;

    case PACKET_VDP_DATA_BLOCK:
        if (packet->length == 0) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        for (uint16_t i = 0; i < packet->length; ++i) {
            ++impl->data_writes;
            vDrip9958WriteData(impl->vdp, packet->payload[i]);
        }
        step_non_cpu_command(impl);
        result->accepted = true;
        result->framebuffer_dirty = true;
        return true;

    case PACKET_VDP_PALETTE_WRITE:
        if (packet->length != 1) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        ++impl->palette_writes;
        vDrip9958WritePalette(impl->vdp, packet->payload[0]);
        result->accepted = true;
        result->framebuffer_dirty = true;
        return true;

    case PACKET_VDP_INDIRECT_WRITE:
        if (packet->length != 1) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        ++impl->indirect_writes;
        vDrip9958WriteRegisterIndirect(impl->vdp, packet->payload[0]);
        result->accepted = true;
        result->framebuffer_dirty = true;
        return true;

    /* --- read queries ---------------------------------------------- */
    case PACKET_VDP_STATUS_READ_REQ:
        if (packet->length != 0) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        ++impl->status_reads;
        result->accepted = true;
        result->reply_kind = VIDEO_REPLY_STATUS_BYTE;
        result->reply_value = vDrip9958ReadStatus(impl->vdp);
        return true;

    case PACKET_VDP_DATA_READ_REQ:
        if (packet->length != 0) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        ++impl->data_reads;
        result->accepted = true;
        result->reply_kind = VIDEO_REPLY_DATA_BYTE;
        result->reply_value = vDrip9958ReadData(impl->vdp);
        return true;

    /* --- legacy scroll compatibility ------------------------------- */
    case PACKET_VDP_SCROLL:
        /* Accepted as a full-screen scroll alias. The actual scroll
         * is implemented by the stream decoder (OP_SCROLL_UP). Here
         * we just mark the framebuffer dirty and accept the packet.
         * The dispatch layer routes non-stream packets here; the
         * scroll is a no-op at the low-level port API since the
         * accelerator owns the logical cell buffer. */
        if (packet->length != 1) {
            result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
            return false;
        }
        result->accepted = true;
        result->framebuffer_dirty = true;
        return true;

    /* --- unrecognized ---------------------------------------------- */
    default:
        result->reply_kind = VIDEO_REPLY_PROTOCOL_ERROR;
        return false;
    }
}

static bool vdrip9958_frame_mark(VideoDevice *device, VideoDeviceResult *result)
{
    (void)device;
    result->accepted = true;
    result->presentation_requested = true;
    return true;
}

static bool vdrip9958_render_framebuffer(VideoDevice *device,
                                          uint32_t *framebuffer,
                                          int width, int height)
{
    if (width != VDRIP9958_CANVAS_WIDTH || height != VDRIP9958_CANVAS_HEIGHT) {
        return false;
    }

    Vdrip9958Device *impl = vdrip9958_impl(device);
    VDrip9958DisplayInfo info = vDrip9958GetDisplayInfo(impl->vdp);

    /* Clear entire canvas */
    size_t pixel_count = (size_t)width * (size_t)height;
    for (size_t i = 0; i < pixel_count; ++i) {
        framebuffer[i] = VDRIP9958_CLEAR_COLOR;
    }

    if (info.mode == VDRIP9958_MODE_INVALID) {
        return true;
    }

    /* Center native output */
    int offset_x = (width  - (int)info.width)  / 2;
    int offset_y = (height - (int)info.height) / 2;

    /* Clamp offsets to canvas */
    if (offset_x < 0) offset_x = 0;
    if (offset_y < 0) offset_y = 0;

    for (uint16_t y = 0; y < info.height; ++y) {
        uint32_t *line = &framebuffer[(offset_y + y) * width + offset_x];
        vDrip9958ScanLine(impl->vdp, y, line);
    }

    return true;
}

static bool vdrip9958_is_text_mode(VideoDevice *device)
{
    Vdrip9958Device *impl = vdrip9958_impl(device);
    VDrip9958DisplayInfo info = vDrip9958GetDisplayInfo(impl->vdp);
    return info.mode == VDRIP9958_MODE_TEXT1 || info.mode == VDRIP9958_MODE_TEXT2;
}

static void vdrip9958_destroy(VideoDevice *device)
{
    if (device == NULL) return;

    Vdrip9958Device *impl = vdrip9958_impl(device);
    if (impl != NULL) {
        vDrip9958Destroy(impl->vdp);
    }

    free(device->impl);
    device->impl = NULL;
    free(device);
}

/* ------------------------------------------------------------------
 * Public factory
 * ------------------------------------------------------------------ */
VideoDevice *video_device_vdrip9958_create(void)
{
    VideoDevice *device = calloc(1, sizeof(*device));
    if (device == NULL) return NULL;

    Vdrip9958Device *impl = calloc(1, sizeof(*impl));
    if (impl == NULL) {
        free(device);
        return NULL;
    }

    impl->vdp = vDrip9958New();
    if (impl->vdp == NULL) {
        free(impl);
        free(device);
        return NULL;
    }

    static const VideoDeviceOps ops = {
        .reset              = vdrip9958_reset,
        .handle_packet      = vdrip9958_handle_packet,
        .frame_mark         = vdrip9958_frame_mark,
        .render_framebuffer = vdrip9958_render_framebuffer,
        .tick_frame         = NULL,
        .is_text_mode       = vdrip9958_is_text_mode,
        .destroy            = vdrip9958_destroy,
    };

    device->ops = &ops;
    device->impl = impl;
    device->info.name = "vdrip9958";
    device->info.width = VDRIP9958_CANVAS_WIDTH;
    device->info.height = VDRIP9958_CANVAS_HEIGHT;
    device->info.supports_status_read = true;
    device->info.supports_data_read = true;
    device->info.supports_palette_port = true;
    device->info.supports_indirect_port = true;
    device->info.allows_proxy_cursor_overlay = false;

    return device;
}
