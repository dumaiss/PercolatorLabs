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
    uint8_t display_offset;
} Vdrip9958Device;

#define VDRIP9958_CANVAS_WIDTH   512
#define VDRIP9958_CANVAS_HEIGHT  424
#define VDRIP9958_CLEAR_COLOR    0x00000000u
#define VDRIP9958_VRAM_SIZE      0x20000u
#define VDRIP9958_G6_PITCH       256u
#define VDRIP9958_BITMAP_BASE    0x00000u

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

static void write_register(Vdrip9958Device *impl, uint8_t reg, uint8_t value)
{
    vDrip9958WriteControl(impl->vdp, value);
    vDrip9958WriteControl(impl->vdp, (uint8_t)(0x80u | (reg & 0x3Fu)));
}

static void set_vram_address(Vdrip9958Device *impl, uint32_t address, bool write_mode)
{
    address &= 0x1FFFFu;
    write_register(impl, 14, (uint8_t)((address >> 14) & 0x07u));
    vDrip9958WriteControl(impl->vdp, (uint8_t)address);
    vDrip9958WriteControl(
        impl->vdp,
        (uint8_t)(((address >> 8) & 0x3Fu) | (write_mode ? 0x40u : 0u)));
}

static uint8_t read_vram(Vdrip9958Device *impl, uint32_t address)
{
    set_vram_address(impl, address, false);
    return vDrip9958ReadData(impl->vdp);
}

static void write_vram(Vdrip9958Device *impl, uint32_t address, uint8_t value)
{
    set_vram_address(impl, address, true);
    vDrip9958WriteData(impl->vdp, value);
}

static void write_vram_block(
    Vdrip9958Device *impl, uint32_t address, const uint8_t *data, uint16_t length)
{
    set_vram_address(impl, address, true);
    for (uint16_t i = 0; i < length; ++i) {
        vDrip9958WriteData(impl->vdp, data[i]);
    }
}

static void fill_vram(
    Vdrip9958Device *impl, uint32_t address, uint8_t value, uint32_t length)
{
    set_vram_address(impl, address, true);
    for (uint32_t i = 0; i < length; ++i) {
        vDrip9958WriteData(impl->vdp, value);
    }
}

static bool stream_ready(Vdrip9958Device *impl, const StreamState *state)
{
    VDrip9958DisplayInfo info = vDrip9958GetDisplayInfo(impl->vdp);
    return state->screen_configured && state->glyph_configured &&
           state->atlas_configured &&
           info.mode == VDRIP9958_MODE_GRAPHIC6;
}

static uint32_t cell_address(const StreamState *state, uint8_t col, uint8_t row)
{
    return state->screen_base +
           ((uint32_t)row * TEXT_COLS + (uint32_t)col) * CELL_BYTES;
}

static void read_cell(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t col, uint8_t row, uint8_t cell[CELL_BYTES])
{
    uint32_t address = cell_address(state, col, row);
    for (uint8_t i = 0; i < CELL_BYTES; ++i) {
        cell[i] = read_vram(impl, address + i);
    }
}

static void write_cell(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t col, uint8_t row, const uint8_t cell[CELL_BYTES])
{
    write_vram_block(impl, cell_address(state, col, row), cell, CELL_BYTES);
}

static uint8_t atlas_pixel(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t ch, uint8_t x, uint8_t y)
{
    uint16_t sx = (uint16_t)(ch % state->atlas_cols) * GLYPH_WIDTH + x;
    uint16_t sy = (uint16_t)(ch / state->atlas_cols) * GLYPH_HEIGHT + y;
    uint32_t address = state->glyph_base +
                       (uint32_t)sy * VDRIP9958_G6_PITCH + (sx >> 1);
    uint8_t packed = read_vram(impl, address);
    return (uint8_t)((sx & 1u) ? (packed & 0x0Fu) : (packed >> 4));
}

static void render_cell(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t col, uint8_t row, const uint8_t cell[CELL_BYTES])
{
    uint8_t fg = cell[1] & 0x0Fu;
    uint8_t bg = cell[2] & 0x0Fu;
    if (cell[2] & 0x80u) {
        uint8_t t = fg;
        fg = bg;
        bg = t;
    }

    uint16_t dx = (uint16_t)col * GLYPH_WIDTH;
    uint16_t dy = (uint16_t)((impl->display_offset +
                              (uint16_t)row * GLYPH_HEIGHT) & 0xFFu);
    for (uint8_t y = 0; y < GLYPH_HEIGHT; ++y) {
        for (uint8_t x = 0; x < GLYPH_WIDTH; x += 2) {
            uint8_t left = atlas_pixel(impl, state, cell[0], x, y) ? fg : bg;
            uint8_t right = atlas_pixel(impl, state, cell[0], x + 1, y) ? fg : bg;
            uint32_t address = VDRIP9958_BITMAP_BASE +
                               (uint32_t)(dy + y) * VDRIP9958_G6_PITCH +
                               (uint32_t)(dx + x) / 2u;
            write_vram(impl, address, (uint8_t)((left << 4) | right));
        }
    }
}

static void fill_cells(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t col, uint8_t row, uint8_t width, uint8_t height, uint8_t ch)
{
    uint8_t cell[CELL_BYTES] = {
        ch,
        (uint8_t)(state->foreground & 0x0Fu),
        (uint8_t)((state->background & 0x0Fu) | (state->reverse ? 0x80u : 0u))
    };
    for (uint8_t r = 0; r < height; ++r) {
        for (uint8_t c = 0; c < width; ++c) {
            write_cell(impl, state, (uint8_t)(col + c), (uint8_t)(row + r), cell);
            render_cell(impl, state, (uint8_t)(col + c), (uint8_t)(row + r), cell);
        }
    }
}

static void copy_cells(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t sc, uint8_t sr, uint8_t dc, uint8_t dr, uint8_t width, uint8_t height)
{
    int r0 = 0, r1 = height, rs = 1;
    int c0 = 0, c1 = width, cs = 1;
    if (dr > sr) {
        r0 = height - 1; r1 = -1; rs = -1;
    }
    if (dr == sr && dc > sc) {
        c0 = width - 1; c1 = -1; cs = -1;
    }
    for (int r = r0; r != r1; r += rs) {
        for (int c = c0; c != c1; c += cs) {
            uint8_t cell[CELL_BYTES];
            read_cell(impl, state, (uint8_t)(sc + c), (uint8_t)(sr + r), cell);
            write_cell(impl, state, (uint8_t)(dc + c), (uint8_t)(dr + r), cell);
            render_cell(impl, state, (uint8_t)(dc + c), (uint8_t)(dr + r), cell);
        }
    }
}

static bool valid_rect(uint8_t col, uint8_t row, uint8_t width, uint8_t height)
{
    return width != 0 && height != 0 &&
           (uint16_t)col + width <= TEXT_COLS &&
           (uint16_t)row + height <= TEXT_ROWS;
}

static bool scroll_region(
    Vdrip9958Device *impl, const StreamState *state,
    uint8_t top, uint8_t bottom, int8_t count)
{
    if (top > bottom || bottom >= TEXT_ROWS || count == 0) {
        return false;
    }
    uint8_t rows = (uint8_t)(bottom - top + 1);
    uint8_t amount = (uint8_t)(count < 0 ? -count : count);
    if (amount > rows) amount = rows;
    if (count > 0) {
        if (amount < rows) {
            copy_cells(impl, state, 0, (uint8_t)(top + amount),
                       0, top, TEXT_COLS, (uint8_t)(rows - amount));
        }
        fill_cells(impl, state, 0, (uint8_t)(bottom - amount + 1),
                   TEXT_COLS, amount, 0x20);
    } else {
        if (amount < rows) {
            copy_cells(impl, state, 0, top, 0, (uint8_t)(top + amount),
                       TEXT_COLS, (uint8_t)(rows - amount));
        }
        fill_cells(impl, state, 0, top, TEXT_COLS, amount, 0x20);
    }
    return true;
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
    impl->display_offset = 0;

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

bool video_device_vdrip9958_stream_op(
    VideoDevice *device, uint8_t opcode, const uint8_t *operands,
    uint16_t operand_size, StreamState *state)
{
    if (device == NULL || state == NULL ||
        strcmp(device->info.name, "vdrip9958") != 0) {
        return false;
    }
    Vdrip9958Device *impl = vdrip9958_impl(device);

    switch (opcode) {
    case OP_DATA_WRITE:
        if (operand_size != 1) return false;
        vDrip9958WriteData(impl->vdp, operands[0]);
        return true;
    case OP_CTRL_WRITE:
        if (operand_size != 1) return false;
        vDrip9958WriteControl(impl->vdp, operands[0]);
        return true;
    case OP_PALETTE_WRITE:
        if (operand_size != 1) return false;
        vDrip9958WritePalette(impl->vdp, operands[0]);
        return true;
    case OP_INDIRECT_WRITE:
        if (operand_size != 1) return false;
        vDrip9958WriteRegisterIndirect(impl->vdp, operands[0]);
        return true;
    case OP_REG_BLOCK: {
        if (operand_size < 2 || operands[1] != operand_size - 2 ||
            (uint16_t)operands[0] + operands[1] > 64) return false;
        for (uint8_t i = 0; i < operands[1]; ++i) {
            write_register(impl, (uint8_t)(operands[0] + i), operands[2 + i]);
        }
        step_non_cpu_command(impl);
        return true;
    }
    case OP_VRAM_ADDR_WRITE: {
        if (operand_size < 4 || operands[3] != operand_size - 4) return false;
        uint32_t address = (uint32_t)operands[0] |
                           ((uint32_t)operands[1] << 8) |
                           ((uint32_t)operands[2] << 16);
        if (address + operands[3] > VDRIP9958_VRAM_SIZE) return false;
        write_vram_block(impl, address, &operands[4], operands[3]);
        state->vram_address = address + operands[3];
        state->vram_addr_pending = false;
        return true;
    }
    case OP_VRAM_SEQ_WRITE: {
        if (operand_size < 2) return false;
        uint16_t count = (uint16_t)operands[0] | ((uint16_t)operands[1] << 8);
        if (count != operand_size - 2 ||
            state->vram_address + count > VDRIP9958_VRAM_SIZE) return false;
        write_vram_block(impl, state->vram_address, &operands[2], count);
        state->vram_address += count;
        state->vram_addr_pending = false;
        return true;
    }
    case OP_COMMAND_SETUP:
        if (operand_size != 15) return false;
        for (uint8_t reg = 32; reg <= 46; ++reg) {
            write_register(impl, reg, operands[reg - 32]);
        }
        step_non_cpu_command(impl);
        return true;
    case OP_TEXT_RUN: {
        if (!stream_ready(impl, state) || operand_size < 4) return false;
        uint8_t flags = operands[0];
        uint8_t col = operands[1];
        uint8_t row = operands[2];
        uint16_t index = 3;
        uint8_t fg = state->foreground;
        uint8_t bg = state->background;
        bool reverse = state->reverse;
        if (flags & 1u) {
            if (operand_size < 7) return false;
            fg = operands[index++] & 0x0F;
            bg = operands[index++] & 0x0F;
            reverse = (operands[index++] & 1u) != 0;
        }
        uint8_t count = operands[index++];
        if (index + count != operand_size || row >= TEXT_ROWS ||
            (uint16_t)col + count > TEXT_COLS) return false;
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t cell[CELL_BYTES] = {
                operands[index + i], fg,
                (uint8_t)(bg | (reverse ? 0x80u : 0u))
            };
            write_cell(impl, state, (uint8_t)(col + i), row, cell);
            render_cell(impl, state, (uint8_t)(col + i), row, cell);
        }
        return true;
    }
    case OP_CELL_FILL:
        if (!stream_ready(impl, state) || operand_size != 5 ||
            !valid_rect(operands[0], operands[1], operands[2], operands[3])) return false;
        fill_cells(impl, state, operands[0], operands[1], operands[2], operands[3], operands[4]);
        return true;
    case OP_CELL_COPY:
        if (!stream_ready(impl, state) || operand_size != 6 ||
            !valid_rect(operands[0], operands[1], operands[4], operands[5]) ||
            !valid_rect(operands[2], operands[3], operands[4], operands[5])) return false;
        copy_cells(impl, state, operands[0], operands[1], operands[2], operands[3],
                   operands[4], operands[5]);
        return true;
    case OP_INSERT_LINES: {
        if (!stream_ready(impl, state) || operand_size != 4) return false;
        uint8_t row = operands[0], count = operands[1], top = operands[2], bottom = operands[3];
        if (top > row || row > bottom || bottom >= TEXT_ROWS || count == 0) return false;
        uint8_t available = (uint8_t)(bottom - row + 1);
        if (count > available) count = available;
        return scroll_region(impl, state, row, bottom, (int8_t)-count);
    }
    case OP_DELETE_LINES: {
        if (!stream_ready(impl, state) || operand_size != 4) return false;
        uint8_t row = operands[0], count = operands[1], top = operands[2], bottom = operands[3];
        if (top > row || row > bottom || bottom >= TEXT_ROWS || count == 0) return false;
        uint8_t available = (uint8_t)(bottom - row + 1);
        if (count > available) count = available;
        return scroll_region(impl, state, row, bottom, (int8_t)count);
    }
    case OP_ERASE_EOL:
        if (!stream_ready(impl, state) || operand_size != 2 ||
            operands[0] >= TEXT_COLS || operands[1] >= TEXT_ROWS) return false;
        fill_cells(impl, state, operands[0], operands[1],
                   (uint8_t)(TEXT_COLS - operands[0]), 1, 0x20);
        return true;
    case OP_CLEAR_SCREEN:
        if (!stream_ready(impl, state) || operand_size != 0) return false;
        /* The terminal occupies source lines 8..199. Clear the complete
         * 212-line G6 page as well so the top/bottom margins cannot expose
         * uninitialised VRAM. */
        fill_vram(
            impl, VDRIP9958_BITMAP_BASE,
            (uint8_t)((state->background & 0x0Fu) * 0x11u),
            VDRIP9958_G6_PITCH * 212u);
        fill_cells(impl, state, 0, 0, TEXT_COLS, TEXT_ROWS, 0x20);
        return true;
    case OP_SCROLL_REGION:
        if (!stream_ready(impl, state) || operand_size != 3) return false;
        return scroll_region(impl, state, operands[0], operands[1], (int8_t)operands[2]);
    case OP_SCROLL_UP:
        if (!stream_ready(impl, state) || operand_size != 1 || operands[0] == 0) return false;
        return scroll_region(impl, state, 0, TEXT_ROWS - 1, (int8_t)operands[0]);
    case OP_SET_DISP_OFFSET:
        if (operand_size != 1 || (operands[0] & 7u) != 0) return false;
        impl->display_offset = operands[0];
        return true;
    default:
        return true;
    }
}
