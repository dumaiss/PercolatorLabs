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
    dispatch->frame_changed = NULL;
    dispatch->frame_changed_userdata = NULL;
    dispatch->keyboard_transport = NULL;
    dispatch->pty_console = NULL;
    dispatch->serial_port = NULL;
    dispatch->storage_backend = NULL;
    dispatch->log_storage = false;
    dispatch->log_packets = false;
    virtual_text_cursor_init(&dispatch->cursor);
    dispatch->packet_count = 0;
}

void packet_dispatch_set_frame_changed_callback(
    PacketDispatch *dispatch,
    FrameChangedCallback callback,
    void *userdata)
{
    dispatch->frame_changed = callback;
    dispatch->frame_changed_userdata = userdata;
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

void packet_dispatch_render(PacketDispatch *dispatch)
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
    pthread_mutex_unlock(dispatch->framebuffer_mutex);

    if (dispatch->frame_changed != NULL) {
        dispatch->frame_changed(dispatch->frame_changed_userdata);
    }
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
        }
        pthread_mutex_unlock(dispatch->framebuffer_mutex);
        if ((result.presentation_requested || result.framebuffer_dirty)
            && dispatch->frame_changed != NULL) {
            dispatch->frame_changed(dispatch->frame_changed_userdata);
        }
        /* Render first, then reset retained accelerator metadata.
         * Upload state survives FRAME_MARK. */
        stream_state_reset(&dispatch->stream_state);
        return;
    }

    if (packet->type == PACKET_PACKET_RESET) {
        /* Full reset: emulator + retained state + upload */
        VideoDeviceResult reset_result;
        video_device_result_clear(&reset_result);
        (void)video_device_reset(dispatch->video_device, &reset_result);
        stream_state_reset(&dispatch->stream_state);
        upload_state_reset(&dispatch->upload_state);
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
        if (stream_result.framebuffer_dirty) {
            packet_dispatch_render(dispatch);
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
    }
    pthread_mutex_unlock(dispatch->framebuffer_mutex);

    dispatch_send_reply(dispatch, &result);

    if (result.framebuffer_dirty && dispatch->frame_changed != NULL) {
        dispatch->frame_changed(dispatch->frame_changed_userdata);
    }
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
