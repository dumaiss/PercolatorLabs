#include "packet_dispatch.h"

#include "packet_replay.h"
#include "protocol_debug.h"
#include "storage_protocol.h"

#include <stdio.h>

/*
 * PacketDispatch is the point where transport-neutral packets meet the selected
 * video backend. It does not know how the backend emulates a chip, and it does
 * not know how the display serves pixels; it just coordinates packet handling,
 * rendering, and dirty notification.
 */

/*
 * Dirty-rect accumulation can slightly under-cover the pixels a backend
 * actually touches (e.g. glyph antialiasing or cursor cells that bleed past
 * the reported region). Pad the presented region by a few pixels on every
 * side, clamped to the framebuffer, so those stragglers get refreshed.
 */
#define DIRTY_RECT_MARGIN 8

void packet_dispatch_init(
    PacketDispatch *dispatch,
    VideoDevice *video_device,
    uint32_t *framebuffer,
    int framebuffer_width,
    int framebuffer_height,
    pthread_mutex_t *framebuffer_mutex)
{
    dispatch->video_device = video_device;
    dispatch->framebuffer = framebuffer;
    dispatch->framebuffer_width = framebuffer_width;
    dispatch->framebuffer_height = framebuffer_height;
    dispatch->framebuffer_mutex = framebuffer_mutex;
    dispatch->present = NULL;
    dispatch->present_userdata = NULL;
    dispatch->keyboard_transport = NULL;
    dispatch->pty_console = NULL;
    dispatch->serial_port = NULL;
    dispatch->storage_backend = NULL;
    dispatch->log_storage = false;
    dispatch->log_packets = false;
    virtual_text_cursor_init(&dispatch->cursor);
    dispatch->packet_count = 0;
    dispatch->pending_dirty = false;
    dispatch->pending_dirty_full = false;
    dispatch->pending_dirty_x = 0;
    dispatch->pending_dirty_y = 0;
    dispatch->pending_dirty_w = 0;
    dispatch->pending_dirty_h = 0;
}

void packet_dispatch_set_present_callback(
    PacketDispatch *dispatch,
    FramePresentCallback callback,
    void *userdata)
{
    dispatch->present = callback;
    dispatch->present_userdata = userdata;
}

void packet_dispatch_set_render_framebuffer(
    PacketDispatch *dispatch, uint32_t *framebuffer)
{
    dispatch->framebuffer = framebuffer;
}

/*
 * Publish the just-rendered framebuffer through the display callback and adopt
 * the buffer it hands back as the next render target. Must be called with the
 * framebuffer mutex held so the swap is serialized against other renderers.
 */
static void packet_dispatch_publish(
    PacketDispatch *dispatch, int x, int y, int width, int height)
{
    if (dispatch->present != NULL) {
        dispatch->framebuffer = dispatch->present(
            dispatch->present_userdata, dispatch->framebuffer,
            x, y, width, height);
    }
}

void packet_dispatch_set_keyboard_transport(PacketDispatch *dispatch, KeyboardTransport *keyboard_transport)
{
    dispatch->keyboard_transport = keyboard_transport;
}

void packet_dispatch_set_pty_console(PacketDispatch *dispatch, PtyConsole *pty_console)
{
    dispatch->pty_console = pty_console;
}

void packet_dispatch_set_storage_backend(
    PacketDispatch *dispatch,
    StorageBackend *storage_backend,
    SerialPort *serial_port,
    bool log_storage)
{
    dispatch->storage_backend = storage_backend;
    dispatch->serial_port = serial_port;
    dispatch->log_storage = log_storage;
}

void packet_dispatch_set_packet_logging(PacketDispatch *dispatch, bool log_packets)
{
    dispatch->log_packets = log_packets;
}

static void packet_dispatch_render_region(
    PacketDispatch *dispatch, int x, int y, int width, int height)
{
    pthread_mutex_lock(dispatch->framebuffer_mutex);
    (void)video_device_render_framebuffer(
        dispatch->video_device,
        dispatch->framebuffer,
        dispatch->framebuffer_width,
        dispatch->framebuffer_height);
    if (dispatch->video_device->info.allows_proxy_cursor_overlay) {
        virtual_text_cursor_render_overlay(
            &dispatch->cursor,
            dispatch->framebuffer,
            dispatch->framebuffer_width,
            dispatch->framebuffer_height,
            video_device_is_text_mode(dispatch->video_device));
    }
    packet_dispatch_publish(dispatch, x, y, width, height);
    pthread_mutex_unlock(dispatch->framebuffer_mutex);
}

void packet_dispatch_render(PacketDispatch *dispatch)
{
    packet_dispatch_render_region(
        dispatch, 0, 0,
        dispatch->framebuffer_width, dispatch->framebuffer_height);
}

static void packet_dispatch_accumulate_dirty(
    PacketDispatch *dispatch, bool full, int x, int y, int width, int height)
{
    if (full || width <= 0 || height <= 0) {
        dispatch->pending_dirty = true;
        dispatch->pending_dirty_full = true;
        dispatch->pending_dirty_x = 0;
        dispatch->pending_dirty_y = 0;
        dispatch->pending_dirty_w = dispatch->framebuffer_width;
        dispatch->pending_dirty_h = dispatch->framebuffer_height;
        return;
    }
    if (dispatch->pending_dirty_full) {
        return;
    }

    if (!dispatch->pending_dirty) {
        dispatch->pending_dirty = true;
        dispatch->pending_dirty_x = x;
        dispatch->pending_dirty_y = y;
        dispatch->pending_dirty_w = width;
        dispatch->pending_dirty_h = height;
        return;
    }

    int x2 = dispatch->pending_dirty_x + dispatch->pending_dirty_w;
    int y2 = dispatch->pending_dirty_y + dispatch->pending_dirty_h;
    int new_x2 = x + width;
    int new_y2 = y + height;
    if (x < dispatch->pending_dirty_x) dispatch->pending_dirty_x = x;
    if (y < dispatch->pending_dirty_y) dispatch->pending_dirty_y = y;
    if (new_x2 > x2) x2 = new_x2;
    if (new_y2 > y2) y2 = new_y2;
    dispatch->pending_dirty_w = x2 - dispatch->pending_dirty_x;
    dispatch->pending_dirty_h = y2 - dispatch->pending_dirty_y;
}

static void packet_dispatch_present_pending(PacketDispatch *dispatch)
{
    if (!dispatch->pending_dirty) {
        return;
    }

    int x = dispatch->pending_dirty_x;
    int y = dispatch->pending_dirty_y;
    int x2 = x + dispatch->pending_dirty_w;
    int y2 = y + dispatch->pending_dirty_h;

    /* Expand by a margin on every side, clamped to the framebuffer. */
    x -= DIRTY_RECT_MARGIN;
    y -= DIRTY_RECT_MARGIN;
    x2 += DIRTY_RECT_MARGIN;
    y2 += DIRTY_RECT_MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > dispatch->framebuffer_width) x2 = dispatch->framebuffer_width;
    if (y2 > dispatch->framebuffer_height) y2 = dispatch->framebuffer_height;

    packet_dispatch_render_region(
        dispatch, x, y, x2 - x, y2 - y);
    dispatch->pending_dirty = false;
    dispatch->pending_dirty_full = false;
    dispatch->pending_dirty_x = 0;
    dispatch->pending_dirty_y = 0;
    dispatch->pending_dirty_w = 0;
    dispatch->pending_dirty_h = 0;
}

void packet_dispatch_tick(PacketDispatch *dispatch)
{
    if (virtual_text_cursor_update_blink(&dispatch->cursor, virtual_text_cursor_now_ms())) {
        packet_dispatch_render(dispatch);
    }
}

void packet_dispatch_destroy(PacketDispatch *dispatch)
{
    if (dispatch == NULL) {
        return;
    }

    virtual_text_cursor_destroy(&dispatch->cursor);
}

static void dispatch_send_reply(PacketDispatch *dispatch, const VideoDeviceResult *result)
{
    if (result->reply_kind == VIDEO_REPLY_NONE) {
        return;
    }

    uint8_t reply_type;
    switch (result->reply_kind) {
    case VIDEO_REPLY_STATUS_BYTE:
        reply_type = PACKET_VDP_STATUS_REPLY;
        break;
    case VIDEO_REPLY_DATA_BYTE:
        reply_type = PACKET_VDP_DATA_REPLY;
        break;
    case VIDEO_REPLY_PROTOCOL_ERROR:
        reply_type = PACKET_PROTOCOL_ERROR;
        break;
    default:
        return;
    }

    serial_port_send_packet(dispatch->serial_port, reply_type,
                            &result->reply_value, 1);
}

void packet_dispatch_handle_packet(const Packet *packet, size_t offset, void *userdata)
{
    PacketDispatch *dispatch = (PacketDispatch *)userdata;
    VideoDeviceResult result;

    (void)offset;

    if (storage_protocol_handle_packet(
            packet,
            dispatch->serial_port,
            dispatch->keyboard_transport,
            dispatch->pty_console,
            dispatch->storage_backend,
            dispatch->log_storage)) {
        return;
    }

    keyboard_transport_note_incoming_packet(dispatch->keyboard_transport, packet);
    dispatch->packet_count++;
    if (dispatch->log_packets) {
        print_packet(dispatch->packet_count, offset, packet);
    }

    if (pty_console_handle_packet(dispatch->pty_console, packet)) {
        return;
    }

    if (packet->type == PACKET_FRAME_MARK) {
        (void)video_device_frame_mark(dispatch->video_device, &result);
        dispatch_send_reply(dispatch, &result);
        pthread_mutex_lock(dispatch->framebuffer_mutex);
        if (result.presentation_requested || result.framebuffer_dirty) {
            (void)video_device_render_framebuffer(
                dispatch->video_device,
                dispatch->framebuffer,
                dispatch->framebuffer_width,
                dispatch->framebuffer_height);
            if (dispatch->video_device->info.allows_proxy_cursor_overlay) {
                virtual_text_cursor_render_overlay(
                    &dispatch->cursor,
                    dispatch->framebuffer,
                    dispatch->framebuffer_width,
                    dispatch->framebuffer_height,
                    video_device_is_text_mode(dispatch->video_device));
            }
            packet_dispatch_publish(
                dispatch, 0, 0,
                dispatch->framebuffer_width, dispatch->framebuffer_height);
        }
        pthread_mutex_unlock(dispatch->framebuffer_mutex);
        /* Render first, then reset retained accelerator metadata.
         * Upload state survives FRAME_MARK. */
        stream_state_reset(&dispatch->stream_state);
        dispatch->pending_dirty = false;
        dispatch->pending_dirty_full = false;
        return;
    }

    if (packet->type == PACKET_PACKET_RESET) {
        /* Full reset: emulator + retained state + upload */
        VideoDeviceResult reset_result;
        video_device_result_clear(&reset_result);
        (void)video_device_reset(dispatch->video_device, &reset_result);
        stream_state_reset(&dispatch->stream_state);
        upload_state_reset(&dispatch->upload_state);
        dispatch->pending_dirty = false;
        dispatch->pending_dirty_full = false;
        if (reset_result.framebuffer_dirty) {
            packet_dispatch_render(dispatch);
        }
        return;
    }

    if (packet->type == PACKET_COMMAND_STREAM) {
        StreamResult stream_result;
        bool ok = stream_decode(
            packet->payload, packet->length,
            &dispatch->stream_state, &dispatch->upload_state,
            &stream_result, dispatch->video_device);
        if (stream_result.presentation_requested) {
            /* OP_PRESENT ends a VDP burst: release the keyboard gate so
             * storage and keyboard resume. Unlike the FRAME_MARK path, the
             * retained accelerator state (stream_state) is preserved. */
            keyboard_transport_note_present(dispatch->keyboard_transport);
        }
        if (stream_result.framebuffer_dirty) {
            packet_dispatch_accumulate_dirty(
                dispatch,
                stream_result.dirty_full,
                stream_result.dirty_x,
                stream_result.dirty_y,
                stream_result.dirty_w,
                stream_result.dirty_h);
        }
        if (stream_result.presentation_requested) {
            packet_dispatch_present_pending(dispatch);
        }
        (void)ok;
        return;
    }

    if (packet->type == PACKET_CURSOR_COMMAND) {
        bool accepted = virtual_text_cursor_handle_command(
            &dispatch->cursor,
            packet->payload,
            packet->length,
            virtual_text_cursor_now_ms());
        if (!accepted) {
            fprintf(stderr, "  Cursor command ignored: malformed payload\n");
            return;
        }
        packet_dispatch_render(dispatch);
        return;
    }

    /* Route to video backend: VDP writes, reads, palette, indirect, etc. */
    pthread_mutex_lock(dispatch->framebuffer_mutex);
    (void)video_device_handle_packet(dispatch->video_device, packet, &result);
    if (result.framebuffer_dirty) {
        (void)video_device_render_framebuffer(
            dispatch->video_device,
            dispatch->framebuffer,
            dispatch->framebuffer_width,
            dispatch->framebuffer_height);
        if (dispatch->video_device->info.allows_proxy_cursor_overlay) {
            virtual_text_cursor_render_overlay(
                &dispatch->cursor,
                dispatch->framebuffer,
                dispatch->framebuffer_width,
                dispatch->framebuffer_height,
                video_device_is_text_mode(dispatch->video_device));
        }
        int width = result.dirty_w;
        int height = result.dirty_h;
        int x = result.dirty_x;
        int y = result.dirty_y;
        if (width <= 0 || height <= 0) {
            x = 0;
            y = 0;
            width = dispatch->framebuffer_width;
            height = dispatch->framebuffer_height;
        }
        packet_dispatch_publish(dispatch, x, y, width, height);
    }
    pthread_mutex_unlock(dispatch->framebuffer_mutex);

    dispatch_send_reply(dispatch, &result);
}

int packet_dispatch_replay_file(PacketDispatch *dispatch, const char *path)
{
    int status = packet_replay_file(path, packet_dispatch_handle_packet, dispatch);
    if (status < 0) {
        return -1;
    }

    printf("Decoded %zu packet%s", dispatch->packet_count, dispatch->packet_count == 1 ? "" : "s");
    if (status > 0) {
        printf(" (framing errors skipped)");
    }
    printf("; video backend: %s\n", dispatch->video_device->info.name);
    return status;
}

size_t packet_dispatch_packet_count(const PacketDispatch *dispatch)
{
    return dispatch->packet_count;
}
