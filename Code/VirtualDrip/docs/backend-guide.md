# Backend Guide — Adding Virtual Video Cards

Virtual Drip backends implement the `VideoDevice` contract (`src/video_device.h`).
Each backend is an independent C module that owns one emulator instance and
translates protocol packets into chip-specific operations.

## The VideoDevice Contract

```c
typedef struct {
    bool (*reset)(VideoDevice *device, VideoDeviceResult *result);
    bool (*handle_packet)(VideoDevice *device, const Packet *packet, VideoDeviceResult *result);
    bool (*frame_mark)(VideoDevice *device, VideoDeviceResult *result);
    bool (*render_framebuffer)(VideoDevice *device, uint32_t *framebuffer, int w, int h);
    void (*tick_frame)(VideoDevice *device, VideoDeviceResult *result);
    bool (*is_text_mode)(VideoDevice *device);
    void (*destroy)(VideoDevice *device);
} VideoDeviceOps;
```

`frame_mark` may be NULL (defaults to presentation-only). `tick_frame` may
be NULL. All others are required for normal operation.

## VideoDeviceInfo

```c
typedef struct {
    const char *name;                    // displayed in logs
    int width, height;                   // canvas dimensions
    bool supports_status_read;           // read replies supported
    bool supports_data_read;
    bool supports_palette_port;          // V9958 port 2
    bool supports_indirect_port;         // V9958 port 3
    bool allows_proxy_cursor_overlay;    // false for native-text backends
} VideoDeviceInfo;
```

## VideoDeviceResult

Backends report operation outcomes through `VideoDeviceResult`:
- `accepted`: packet was recognized and valid
- `framebuffer_dirty`: render_framebuffer needed
- `presentation_requested`: explicit render trigger (FRAME_MARK, OP_PRESENT)
- `reply_kind`: `VIDEO_REPLY_NONE`, `_STATUS_BYTE`, `_DATA_BYTE`, `_PROTOCOL_ERROR`
- `reply_value`: the status/data/error byte

Dispatch owns serial reply encoding. Backends must not call serial or VNC APIs.

## Adding a New Backend

1. Create `src/video_device_<chip>.c/.h`
2. Implement the `VideoDeviceOps` vtable
3. Add `video_device_<chip>_create()` factory function
4. Register in `main.c:create_video_backend()`
5. Add `--video-backend <name>` to `app_config.c`
6. Add the emulator library via `add_subdirectory()` in `CMakeLists.txt`
7. Set `VideoDeviceInfo` fields appropriately

## Supported Packet Subsets

Backends handle only the packets they recognize. The dispatch layer routes
all non-storage/non-PTY/non-control packets to `handle_packet`. Unrecognized
packet types should set `accepted = false` and `reply_kind =
VIDEO_REPLY_PROTOCOL_ERROR` (for read requests) or silently ignore (for writes).

### TMS9918-compatible subset
`VDP_CTRL_WRITE`, `VDP_DATA_WRITE`, `VDP_DATA_BLOCK`, `RESET`, `FRAME_MARK`

### V9958 extended subset
All of above + `VDP_PALETTE_WRITE`, `VDP_INDIRECT_WRITE`,
`VDP_STATUS_READ_REQ`, `VDP_DATA_READ_REQ`, `VDP_SCROLL` (legacy alias),
`COMMAND_STREAM`, `PACKET_RESET`

## Design Rules

- Backends must not include serial, VNC, or dispatch headers
- Backends must not access emulator-private state
- Canvas dimensions are fixed for the process lifetime
- Presentation is requested, not performed, by the backend
- Common operations should represent genuinely common hardware concepts

---

*Derived from `src/video_device.h`, `src/video_device_vdrip9958.c`. Verified 2026-06-20.*
