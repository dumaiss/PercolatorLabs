#include "protocol.h"
#include "video_device_vdrip9958.h"

#include <string.h>

/*
 * Protocol helpers are deliberately small and dependency-free so every packet
 * source and sink can share the same encoder and type interpretation.
 */

bool packet_encode(
    const Packet *packet,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (packet->length > MAX_PACKET_PAYLOAD) {
        return false;
    }

    size_t total = PACKET_SYNC_SIZE + 2u + 1u + packet->length;
    if (output_capacity < total) {
        return false;
    }

    uint16_t declared = (uint16_t)(packet->length + 1u);

    output[0] = PACKET_SYNC0;
    output[1] = PACKET_SYNC1;
    output[2] = (uint8_t)(declared);
    output[3] = (uint8_t)(declared >> 8);
    output[4] = packet->type;
    if (packet->length > 0) {
        memcpy(&output[5], packet->payload, packet->length);
    }

    *output_length = total;
    return true;
}

const char *packet_type_name(uint8_t type)
{
    switch (type) {
    case PACKET_VDP_CTRL_WRITE:
        return "VDP_CTRL_WRITE";
    case PACKET_VDP_DATA_WRITE:
        return "VDP_DATA_WRITE";
    case PACKET_VDP_STATUS_READ:
        return "VDP_STATUS_READ";
    case PACKET_VDP_DATA_READ:
        return "VDP_DATA_READ";
    case PACKET_TERMINAL_INPUT:
        return "TERMINAL_INPUT";
    case PACKET_RESET:
        return "RESET";
    case PACKET_PING:
        return "PING";
    case PACKET_FRAME_MARK:
        return "FRAME_MARK";
    case PACKET_CURSOR_COMMAND:
        return "CURSOR_COMMAND";
    case PACKET_PROXY_READY:
        return "PROXY_READY";
    case PACKET_VDP_DATA_BLOCK:
        return "VDP_DATA_BLOCK";
    case PACKET_VDP_SCROLL:
        return "VDP_SCROLL";
    case PACKET_STORAGE_READ_REQ:
        return "STORAGE_READ_REQ";
    case PACKET_STORAGE_READ_REPLY:
        return "STORAGE_READ_REPLY";
    case PACKET_STORAGE_WRITE_REQ:
        return "STORAGE_WRITE_REQ";
    case PACKET_STORAGE_WRITE_REPLY:
        return "STORAGE_WRITE_REPLY";
    case PACKET_TERMINAL_TX:
        return "TERMINAL_TX";
    case PACKET_TERMINAL_RX:
        return "TERMINAL_RX";
    case PACKET_VDP_PALETTE_WRITE:
        return "VDP_PALETTE_WRITE";
    case PACKET_VDP_INDIRECT_WRITE:
        return "VDP_INDIRECT_WRITE";
    case PACKET_VDP_STATUS_READ_REQ:
        return "VDP_STATUS_READ_REQ";
    case PACKET_VDP_DATA_READ_REQ:
        return "VDP_DATA_READ_REQ";
    case PACKET_VDP_STATUS_REPLY:
        return "VDP_STATUS_REPLY";
    case PACKET_VDP_DATA_REPLY:
        return "VDP_DATA_REPLY";
    case PACKET_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    case PACKET_COMMAND_STREAM:
        return "COMMAND_STREAM";
    case PACKET_VRAM_UPLOAD_BEGIN:
        return "VRAM_UPLOAD_BEGIN";
    case PACKET_VRAM_UPLOAD_DATA:
        return "VRAM_UPLOAD_DATA";
    case PACKET_VRAM_UPLOAD_END:
        return "VRAM_UPLOAD_END";
    case PACKET_PACKET_RESET:
        return "PACKET_RESET";
    default:
        return "UNKNOWN";
    }
}

/* ==================================================================
 * Command stream decoder (UOW-3)
 * ================================================================== */

#include <stdio.h>
#include <stdlib.h>

/* Opcode descriptor table */
static const OpcodeDescriptor opcode_table[256] = {
    [OP_DATA_WRITE]       = { true, false, 1 },
    [OP_CTRL_WRITE]       = { true, false, 1 },
    [OP_PALETTE_WRITE]    = { true, false, 1 },
    [OP_INDIRECT_WRITE]   = { true, false, 1 },
    [OP_REG_BLOCK]        = { true, true,  0 },
    [OP_VRAM_ADDR_WRITE]  = { true, true,  0 },
    [OP_VRAM_SEQ_WRITE]   = { true, true,  0 },
    [OP_COMMAND_SETUP]    = { true, false, 15 },
    [OP_TEXT_RUN]         = { true, true,  0 },
    [OP_CELL_FILL]        = { true, false, 5 },
    [OP_CELL_COPY]        = { true, false, 6 },
    [OP_INSERT_LINES]     = { true, false, 4 },
    [OP_DELETE_LINES]     = { true, false, 4 },
    [OP_ERASE_EOL]        = { true, false, 2 },
    [OP_CLEAR_SCREEN]     = { true, false, 0 },
    [OP_SCROLL_REGION]    = { true, false, 3 },
    [OP_SCROLL_UP]        = { true, false, 1 },
    [OP_SET_CURSOR]       = { true, false, 2 },
    [OP_SET_ATTR]         = { true, false, 3 },
    [OP_SET_VRAM_ADDR]    = { true, false, 3 },
    [OP_SET_SCREEN_BASE]  = { true, false, 3 },
    [OP_SET_GLYPH_BASE]   = { true, false, 3 },
    [OP_SET_ATLAS_CONFIG] = { true, false, 1 },
    [OP_SET_DISP_OFFSET]  = { true, false, 1 },
    [OP_UPLOAD_BEGIN]     = { true, false, 7 },
    [OP_UPLOAD_DATA]      = { true, true,  0 },
    [OP_UPLOAD_END]       = { true, false, 0 },
    [OP_PRESENT]          = { true, false, 0 },
    [OP_NOP]              = { true, false, 0 },
};

const OpcodeDescriptor *stream_opcode_descriptor(uint8_t opcode)
{
    return &opcode_table[opcode];
}

void stream_state_reset(StreamState *state)
{
    memset(state, 0, sizeof(*state));
    state->foreground = 15;
}

void upload_state_reset(UploadState *upload)
{
    if (upload->shadow != NULL) {
        free(upload->shadow);
    }
    memset(upload, 0, sizeof(*upload));
}

/* ------------------------------------------------------------------
 * Stream decoder — walks payload, dispatches opcodes.
 *
 * Calls stub functions for each opcode family. The actual emulator
 * interaction is injected via the device callback context.
 * ------------------------------------------------------------------ */

static uint32_t read_u24_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

#define STREAM_CANVAS_WIDTH 512
#define STREAM_CANVAS_HEIGHT 424
#define STREAM_CELL_WIDTH 6
#define STREAM_CELL_HEIGHT 16
#define STREAM_CURSOR_SAT_BASE 0x1F200u
#define STREAM_CURSOR_HIDDEN_Y 0xD8u

static void stream_dirty_add(
    StreamResult *result, int x, int y, int width, int height)
{
    if (result->dirty_full || width <= 0 || height <= 0) {
        return;
    }

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > STREAM_CANVAS_WIDTH) {
        width = STREAM_CANVAS_WIDTH - x;
    }
    if (y + height > STREAM_CANVAS_HEIGHT) {
        height = STREAM_CANVAS_HEIGHT - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    if (result->dirty_w <= 0 || result->dirty_h <= 0) {
        result->dirty_x = x;
        result->dirty_y = y;
        result->dirty_w = width;
        result->dirty_h = height;
        return;
    }

    int x2 = result->dirty_x + result->dirty_w;
    int y2 = result->dirty_y + result->dirty_h;
    int new_x2 = x + width;
    int new_y2 = y + height;
    if (x < result->dirty_x) result->dirty_x = x;
    if (y < result->dirty_y) result->dirty_y = y;
    if (new_x2 > x2) x2 = new_x2;
    if (new_y2 > y2) y2 = new_y2;
    result->dirty_w = x2 - result->dirty_x;
    result->dirty_h = y2 - result->dirty_y;
}

static void stream_dirty_full(StreamResult *result)
{
    result->framebuffer_dirty = true;
    result->dirty_full = true;
    result->dirty_x = 0;
    result->dirty_y = 0;
    result->dirty_w = STREAM_CANVAS_WIDTH;
    result->dirty_h = STREAM_CANVAS_HEIGHT;
}

static void stream_dirty_cells(
    StreamResult *result, const StreamState *state,
    uint8_t col, uint8_t row, uint8_t width, uint8_t height)
{
    result->framebuffer_dirty = true;
    stream_dirty_add(
        result,
        (int)col * STREAM_CELL_WIDTH,
        ((int)state->display_offset + (int)row * 8) * 2,
        (int)width * STREAM_CELL_WIDTH,
        (int)height * STREAM_CELL_HEIGHT);
}

static void stream_dirty_cursor(
    StreamResult *result, uint8_t x, uint8_t y)
{
    result->framebuffer_dirty = true;
    stream_dirty_add(result, (int)x * 2, ((int)y + 1) * 2, 16, 16);
}

/*
 * A raw VRAM write to the logical-cell buffer (screen_base) or the glyph
 * atlas (glyph_base) is not visible damage by itself: the accelerator renders
 * the displayed pixels from those staging regions, and the cell/accelerator
 * ops report that visible destination separately. Only suppress when the
 * whole write provably lands inside a configured staging region.
 */
static bool stream_vram_is_staging(
    const StreamState *state, uint32_t address, uint32_t count)
{
    if (count == 0) {
        return false;
    }
    uint32_t end = address + count; /* caller bounds count to VRAM size */

    if (state->screen_configured) {
        uint32_t base = state->screen_base;
        uint32_t size = (uint32_t)TEXT_COLS * TEXT_ROWS * CELL_BYTES;
        if (address >= base && end <= base + size) {
            return true;
        }
    }

    if (state->glyph_configured && state->atlas_configured &&
        state->atlas_cols > 0) {
        uint32_t base = state->glyph_base;
        /* 256 glyphs across atlas_cols columns, GLYPH_HEIGHT scanlines per
         * glyph row, 256 bytes per atlas scanline. */
        uint32_t rows = (256u + state->atlas_cols - 1u) / state->atlas_cols;
        uint32_t size = rows * (uint32_t)GLYPH_HEIGHT * 256u;
        if (address >= base && end <= base + size) {
            return true;
        }
    }

    return false;
}

static void stream_dirty_vram_write(
    StreamResult *result, StreamState *state,
    const uint8_t *operands, uint16_t operand_size)
{
    if (operand_size < 4) {
        stream_dirty_full(result);
        return;
    }

    uint32_t address = read_u24_le(operands);
    uint8_t count = operands[3];
    if (address == STREAM_CURSOR_SAT_BASE && count >= 4 &&
        operand_size >= (uint16_t)(4 + count)) {
        if (state->sprite_cursor_known && state->sprite_cursor_visible) {
            stream_dirty_cursor(
                result, state->sprite_cursor_x, state->sprite_cursor_y);
        }

        uint8_t y = operands[4];
        uint8_t x = operands[5];
        bool visible = y != STREAM_CURSOR_HIDDEN_Y;
        if (visible) {
            stream_dirty_cursor(result, x, y);
        }
        state->sprite_cursor_known = true;
        state->sprite_cursor_visible = visible;
        state->sprite_cursor_x = x;
        state->sprite_cursor_y = y;
        return;
    }

    if (stream_vram_is_staging(state, address, count)) {
        return; /* staging write: no visible damage by itself */
    }

    stream_dirty_full(result);
}

bool stream_decode(
    const uint8_t *payload,
    uint16_t length,
    StreamState *state,
    UploadState *upload,
    StreamResult *result,
    void *device)
{
    memset(result, 0, sizeof(*result));
    result->accepted = true;

    uint16_t pos = 0;

    while (pos < length) {
        uint8_t opcode = payload[pos++];
        const OpcodeDescriptor *desc = &opcode_table[opcode];

        if (!desc->known) {
            fprintf(stderr, "stream: unknown opcode 0x%02X at offset %u, stopping\n", opcode, pos - 1);
            break;
        }

        uint16_t operand_size = desc->fixed_size;
        if (desc->variable) {
            /* Determine size from first operands */
            switch (opcode) {
            case OP_REG_BLOCK:
                if (pos + 1 >= length) goto truncated;
                operand_size = 2 + payload[pos + 1];
                break;
            case OP_VRAM_ADDR_WRITE:
                if (pos + 3 >= length) goto truncated;
                operand_size = 4 + payload[pos + 3];
                break;
            case OP_VRAM_SEQ_WRITE:
                if (pos + 1 >= length) goto truncated;
                operand_size = 2 + read_u16_le(&payload[pos]);
                break;
            case OP_TEXT_RUN: {
                if (pos + 3 > length) goto truncated;
                uint16_t count_index = (uint16_t)(pos + 3);
                if (payload[pos] & 0x01) {
                    count_index = (uint16_t)(count_index + 3);
                }
                if (count_index >= length) goto truncated;
                operand_size = (uint16_t)(
                    (count_index - pos) + 1u + payload[count_index]);
                break;
            }
            case OP_UPLOAD_DATA:
                if (pos + 4 >= length) goto truncated;
                operand_size = 5 + read_u16_le(&payload[pos + 3]);
                break;
            default:
                goto truncated;
            }
        }

        if (pos + operand_size > length) {
            truncated:
            fprintf(stderr, "stream: truncated opcode 0x%02X at offset %u\n", opcode, pos - 1);
            break;
        }

        const uint8_t *operands = &payload[pos];
        pos += operand_size;

        /* The device advances vram_address as a side effect, so capture the
         * pre-write address for OP_VRAM_SEQ_WRITE dirty classification. */
        uint32_t vram_addr_before = state->vram_address;

        /* Execute the operation against the selected V9958 device first.
         * Unit tests pass NULL and exercise decoder/state behavior only. */
        bool device_ok = true;
        if (device != NULL) {
            device_ok = video_device_vdrip9958_stream_op(
                (VideoDevice *)device, opcode, operands, operand_size, state);
        }

        /* --- Update retained decoder state/result --- */
        switch (opcode) {
        case OP_DATA_WRITE:
        case OP_CTRL_WRITE:
        case OP_PALETTE_WRITE:
        case OP_INDIRECT_WRITE:
            if (device_ok) {
                stream_dirty_full(result);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_REG_BLOCK:
        case OP_COMMAND_SETUP:
            if (device_ok) {
                stream_dirty_full(result);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_VRAM_SEQ_WRITE:
            if (device_ok) {
                uint16_t seq_count = read_u16_le(operands);
                if (stream_vram_is_staging(
                        state, vram_addr_before, seq_count)) {
                    /* staging write: no visible damage by itself */
                } else {
                    stream_dirty_full(result);
                }
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_VRAM_ADDR_WRITE:
            if (device_ok) {
                stream_dirty_vram_write(result, state, operands, operand_size);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_TEXT_RUN: {
            uint16_t count_index = 3;
            if (operands[0] & 0x01) count_index += 3;
            if (device_ok && count_index < operand_size) {
                stream_dirty_cells(
                    result, state, operands[1], operands[2],
                    operands[count_index], 1);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;
        }

        case OP_CELL_FILL:
            if (device_ok) {
                stream_dirty_cells(
                    result, state, operands[0], operands[1],
                    operands[2], operands[3]);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_CELL_COPY:
            if (device_ok) {
                stream_dirty_cells(
                    result, state, operands[2], operands[3],
                    operands[4], operands[5]);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_INSERT_LINES:
        case OP_DELETE_LINES:
            if (device_ok) {
                stream_dirty_cells(
                    result, state, 0, operands[0],
                    TEXT_COLS, (uint8_t)(operands[3] - operands[0] + 1));
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_ERASE_EOL:
            if (device_ok) {
                stream_dirty_cells(
                    result, state, operands[0], operands[1],
                    (uint8_t)(TEXT_COLS - operands[0]), 1);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_CLEAR_SCREEN:
            if (device_ok) {
                stream_dirty_full(result);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_SCROLL_REGION:
            if (device_ok) {
                stream_dirty_cells(
                    result, state, 0, operands[0], TEXT_COLS,
                    (uint8_t)(operands[1] - operands[0] + 1));
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_SCROLL_UP:
            if (device_ok) {
                stream_dirty_cells(result, state, 0, 0, TEXT_COLS, TEXT_ROWS);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_SET_CURSOR:
            if (operands[0] < TEXT_COLS && operands[1] < TEXT_ROWS) {
                state->cursor_col = operands[0];
                state->cursor_row = operands[1];
                state->cursor_wrap_pending = false;
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_SET_ATTR:
            state->foreground = operands[0] & 0x0F;
            state->background = operands[1] & 0x0F;
            state->reverse = (operands[2] & 0x01) != 0;
            result->ops_executed++;
            break;

        case OP_SET_VRAM_ADDR:
            state->vram_address = read_u24_le(operands);
            state->vram_addr_pending = true;
            result->ops_executed++;
            break;

        case OP_SET_SCREEN_BASE:
            /* Display-page change: re-points the logical cell buffer, so the
             * whole screen must be re-interpreted. */
            state->screen_base = read_u24_le(operands);
            state->screen_configured = true;
            stream_dirty_full(result);
            result->ops_executed++;
            break;

        case OP_SET_GLYPH_BASE:
            /* Changes how every cell maps to pixels: full re-interpretation. */
            state->glyph_base = read_u24_le(operands);
            state->glyph_configured = true;
            stream_dirty_full(result);
            result->ops_executed++;
            break;

        case OP_SET_ATLAS_CONFIG:
            /* Atlas layout change re-interprets every glyph: full invalidate. */
            state->atlas_cols = operands[0];
            state->atlas_configured = true;
            stream_dirty_full(result);
            result->ops_executed++;
            break;

        case OP_SET_DISP_OFFSET:
            if ((operands[0] & 0x07) == 0 && device_ok) {
                state->display_offset = operands[0];
                stream_dirty_full(result);
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_UPLOAD_BEGIN:
            upload_state_reset(upload);
            upload->active = true;
            upload->base_address = read_u24_le(operands);
            upload->total_length = read_u24_le(&operands[3]);
            upload->next_offset = 0;
            upload->shadow = calloc(upload->total_length, 1);
            result->ops_executed++;
            break;

        case OP_UPLOAD_DATA: {
            uint32_t offset = read_u24_le(operands);
            uint16_t data_len = read_u16_le(&operands[3]);
            const uint8_t *data = &operands[5];
            if (!upload->active || !upload->shadow) {
                result->ops_skipped++;
                break;
            }
            if (offset + data_len > upload->total_length) {
                result->ops_skipped++;
                break;
            }
            if (offset < upload->next_offset &&
                offset + data_len <= upload->next_offset) {
                if (memcmp(upload->shadow + offset, data, data_len) != 0) {
                    break; /* different data → reject, stop stream */
                }
                /* exact duplicate: idempotent */
            } else if (offset == upload->next_offset) {
                memcpy(upload->shadow + offset, data, data_len);
                if (device != NULL) {
                    uint8_t address_write[4 + UINT8_MAX];
                    uint32_t address = upload->base_address + offset;
                    uint16_t copied = 0;
                    while (copied < data_len) {
                        uint8_t chunk = (uint8_t)((data_len - copied) > UINT8_MAX
                            ? UINT8_MAX : (data_len - copied));
                        address_write[0] = (uint8_t)address;
                        address_write[1] = (uint8_t)(address >> 8);
                        address_write[2] = (uint8_t)(address >> 16);
                        address_write[3] = chunk;
                        memcpy(&address_write[4], data + copied, chunk);
                        if (!video_device_vdrip9958_stream_op(
                                (VideoDevice *)device, OP_VRAM_ADDR_WRITE,
                                address_write, (uint16_t)(4 + chunk), state)) {
                            result->ops_skipped++;
                            break;
                        }
                        copied = (uint16_t)(copied + chunk);
                        address += chunk;
                    }
                    if (copied != data_len) {
                        break;
                    }
                }
                upload->next_offset = offset + data_len;
            } else {
                break; /* gap or spanning → reject */
            }
            result->ops_executed++;
            break;
        }

        case OP_UPLOAD_END:
            if (upload->active && upload->next_offset >= upload->total_length) {
                upload->active = false;
                free(upload->shadow);
                upload->shadow = NULL;
                result->ops_executed++;
            } else {
                result->ops_skipped++;
            }
            break;

        case OP_PRESENT:
            result->presentation_requested = true;
            result->ops_executed++;
            break;

        case OP_NOP:
            result->ops_executed++;
            break;
        }
    }

    return result->ops_executed > 0 || result->ops_skipped == 0;
}
