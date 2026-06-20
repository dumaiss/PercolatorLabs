#include "protocol.h"

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

bool stream_decode(
    const uint8_t *payload,
    uint16_t length,
    StreamState *state,
    UploadState *upload,
    StreamResult *result,
    void *device)
{
    (void)device;
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
                if (pos >= length) goto truncated;
                uint8_t flags = payload[pos++];
                operand_size = 1; /* consumed flags byte */
                if (flags & 0x01) {
                    operand_size += 3; /* attr override */
                }
                if (pos + operand_size >= length) goto truncated;
                uint8_t count = payload[pos + operand_size];
                operand_size += 3; /* col, row, count */
                operand_size += count; /* chars */
                pos--; /* back up: the +1 for flags was already consumed */
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

        /* --- Execute opcode --- */
        switch (opcode) {
        case OP_DATA_WRITE:
        case OP_CTRL_WRITE:
        case OP_PALETTE_WRITE:
        case OP_INDIRECT_WRITE:
            result->framebuffer_dirty = true;
            result->ops_executed++;
            break;

        case OP_REG_BLOCK:
        case OP_VRAM_ADDR_WRITE:
        case OP_VRAM_SEQ_WRITE:
        case OP_COMMAND_SETUP:
            result->framebuffer_dirty = true;
            result->ops_executed++;
            break;

        case OP_TEXT_RUN:
        case OP_CELL_FILL:
        case OP_CELL_COPY:
        case OP_INSERT_LINES:
        case OP_DELETE_LINES:
        case OP_ERASE_EOL:
        case OP_CLEAR_SCREEN:
        case OP_SCROLL_REGION:
        case OP_SCROLL_UP:
            result->framebuffer_dirty = true;
            result->ops_executed++;
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
            state->screen_base = read_u24_le(operands);
            state->screen_configured = true;
            result->ops_executed++;
            break;

        case OP_SET_GLYPH_BASE:
            state->glyph_base = read_u24_le(operands);
            state->glyph_configured = true;
            result->ops_executed++;
            break;

        case OP_SET_ATLAS_CONFIG:
            state->atlas_cols = operands[0];
            state->atlas_configured = true;
            result->ops_executed++;
            break;

        case OP_SET_DISP_OFFSET:
            state->display_offset = operands[0];
            result->framebuffer_dirty = true;
            result->ops_executed++;
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
