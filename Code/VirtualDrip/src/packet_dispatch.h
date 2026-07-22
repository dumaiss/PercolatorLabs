#ifndef PACKET_DISPATCH_H
#define PACKET_DISPATCH_H

/**
 * @file packet_dispatch.h
 * Bridge decoded packets into the selected VideoDevice and framebuffer.
 *
 * Packet sources call the PacketHandler here. Dispatch sends valid packets to
 * the video backend, renders into the caller-owned framebuffer when the backend
 * reports a dirty frame, and notifies display code through a callback.
 */

#include "protocol.h"
#include "video_device.h"
#include "keyboard_transport.h"
#include "pty_console.h"
#include "serial_port.h"
#include "storage_backend.h"
#include "virtual_text_cursor.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * Display-layer callback invoked after a framebuffer rectangle is rerendered.
 *
 * The display publishes the just-rendered buffer (e.g. by swapping it in as the
 * VNC front buffer and marking the rectangle modified) and returns the buffer
 * the dispatcher should render into next. Returning @p rendered means "no swap"
 * (single-buffered). Called with the framebuffer mutex held.
 */
typedef uint32_t *(*FramePresentCallback)(
    void *userdata, uint32_t *rendered, int x, int y, int width, int height);

/** Dispatch context; borrows the video device, framebuffer, and mutex. */
typedef struct {
    VideoDevice *video_device;
    uint32_t *framebuffer;
    int framebuffer_width;
    int framebuffer_height;
    pthread_mutex_t *framebuffer_mutex;
    FramePresentCallback present;
    void *present_userdata;
    KeyboardTransport *keyboard_transport;
    PtyConsole *pty_console;
    SerialPort *serial_port;
    StorageBackend *storage_backend;
    bool log_storage;
    bool log_packets;
    VirtualTextCursor cursor;
    size_t packet_count;
    StreamState stream_state;
    UploadState upload_state;
    bool pending_dirty;
    bool pending_dirty_full;
    int pending_dirty_x;
    int pending_dirty_y;
    int pending_dirty_w;
    int pending_dirty_h;
    pthread_mutex_t reset_mutex;
    pthread_cond_t reset_cond;
    pthread_t reset_thread;
    struct timespec reset_deadline;
    unsigned reset_generation;
    bool reset_thread_started;
    bool reset_pending;
    bool reset_shutdown;
} PacketDispatch;

/** Initialize dispatch with a backend and host-owned framebuffer. */
void packet_dispatch_init(
    PacketDispatch *dispatch,
    VideoDevice *video_device,
    uint32_t *framebuffer,
    int framebuffer_width,
    int framebuffer_height,
    pthread_mutex_t *framebuffer_mutex);

/** Register the display present callback that publishes rendered frames. */
void packet_dispatch_set_present_callback(
    PacketDispatch *dispatch,
    FramePresentCallback callback,
    void *userdata);

/** Override the buffer the dispatcher renders into (the back buffer). */
void packet_dispatch_set_render_framebuffer(
    PacketDispatch *dispatch, uint32_t *framebuffer);

/** Register optional keyboard transport gate updates for incoming serial packets. */
void packet_dispatch_set_keyboard_transport(PacketDispatch *dispatch, KeyboardTransport *keyboard_transport);

/** Register optional packetized PTY console bridge for TERMINAL_TX packets. */
void packet_dispatch_set_pty_console(PacketDispatch *dispatch, PtyConsole *pty_console);

/** Register optional drive-image storage handling for incoming serial packets. */
void packet_dispatch_set_storage_backend(
    PacketDispatch *dispatch,
    StorageBackend *storage_backend,
    SerialPort *serial_port,
    bool log_storage);

/** Enable optional decoded non-storage packet logging. */
void packet_dispatch_set_packet_logging(PacketDispatch *dispatch, bool log_packets);

/** Render the backend's current state into the framebuffer under the mutex. */
void packet_dispatch_render(PacketDispatch *dispatch);

/** Process local cursor blink and redraw when the blink phase changes. */
void packet_dispatch_tick(PacketDispatch *dispatch);

/** Release dispatch-owned resources. */
void packet_dispatch_destroy(PacketDispatch *dispatch);

/** Hold/reset proxy state after the configured serial device disappears. */
void packet_dispatch_serial_disconnected(PacketDispatch *dispatch);

/** Reset again and schedule delayed READY after the device is reopened. */
void packet_dispatch_serial_reconnected(PacketDispatch *dispatch);

/** PacketHandler implementation used by file replay and serial input. */
void packet_dispatch_handle_packet(const Packet *packet, size_t offset, void *userdata);

/** Replay a file through this dispatch context. */
int packet_dispatch_replay_file(PacketDispatch *dispatch, const char *path);

/** Number of packets observed by this dispatch context. */
size_t packet_dispatch_packet_count(const PacketDispatch *dispatch);

#endif
