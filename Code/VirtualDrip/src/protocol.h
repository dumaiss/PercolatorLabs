#ifndef PROTOCOL_H
#define PROTOCOL_H

/**
 * @file protocol.h
 * Virtual Drip wire protocol definitions.
 *
 * Packets are encoded as:
 *   [SYNC0=0xA5][SYNC1=0x5A][LEN_LO][LEN_HI][TYPE][PAYLOAD...]
 *
 * The two sync bytes are framing bytes. LEN is a 16-bit little-endian count
 * of the TYPE and PAYLOAD bytes (1..1025). There is no CRC, checksum, or
 * other integrity field. The decoded payload length is (declared_length - 1).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Packet stream framing bytes. */
#define PACKET_SYNC0 0xA5
#define PACKET_SYNC1 0x5A
#define PACKET_SYNC_SIZE 2

/** Maximum payload bytes in a decoded packet. */
#define MAX_PACKET_PAYLOAD 1024u

/** Wire declared length includes the type byte. */
#define PACKET_MAX_DECLARED_LENGTH ((uint16_t)(MAX_PACKET_PAYLOAD + 1u))

/** Wire declared length below 1 is invalid (no room for type byte). */
#define PACKET_MIN_DECLARED_LENGTH 1u

/** Maximum encoded frame size: SYNC(2) + LEN(2) + TYPE(1) + PAYLOAD(1024). */
#define PACKET_MAX_WIRE_SIZE (PACKET_SYNC_SIZE + 2u + PACKET_MAX_DECLARED_LENGTH)

/**
 * Packet type values shared by file replay, serial input, and serial output.
 *
 * VDP_* packets flow from Zephyr or replay files into the video backend.
 * TERMINAL_INPUT is a legacy proxy-to-Z80 keyboard packet type; the default
 * live proxy path now sends keyboard input as raw terminal bytes instead.
 * RESET and PING are Virtual Drip control packets, not TMS9928A port writes.
 * FRAME_MARK is a replay/pacing marker for animation tests; it is not a
 * hardware VBlank signal.
 * TERMINAL_TX and TERMINAL_RX are the packetized PTY console byte stream:
 * TX flows from Z80 console output to the proxy PTY, RX flows from PTY input
 * back to the Z80 console input FIFO.
 *
 * Values 0x01-0x12 are preserved from the original protocol. Values 0x13-0x1E
 * are new for the V9958 integration and protocol operations.
 */
typedef enum {
    /* Existing — preserved */
    PACKET_VDP_CTRL_WRITE       = 0x01,
    PACKET_VDP_DATA_WRITE       = 0x02,
    PACKET_VDP_STATUS_READ      = 0x03,
    PACKET_VDP_DATA_READ        = 0x04,
    PACKET_TERMINAL_INPUT       = 0x05,
    PACKET_KEYBOARD_INPUT       = PACKET_TERMINAL_INPUT,
    PACKET_RESET                = 0x06,
    PACKET_PING                 = 0x07,
    PACKET_FRAME_MARK           = 0x08,
    PACKET_CURSOR_COMMAND       = 0x09,
    PACKET_PROXY_READY          = 0x0A,
    PACKET_VDP_DATA_BLOCK       = 0x0B,
    PACKET_VDP_SCROLL           = 0x0C,
    PACKET_STORAGE_READ_REQ     = 0x0D,
    PACKET_STORAGE_READ_REPLY   = 0x0E,
    PACKET_STORAGE_WRITE_REQ    = 0x0F,
    PACKET_STORAGE_WRITE_REPLY  = 0x10,
    PACKET_TERMINAL_TX          = 0x11,
    PACKET_TERMINAL_RX          = 0x12,

    /* New — V9958 port operations */
    PACKET_VDP_PALETTE_WRITE    = 0x13,
    PACKET_VDP_INDIRECT_WRITE   = 0x14,
    PACKET_VDP_STATUS_READ_REQ  = 0x15,
    PACKET_VDP_DATA_READ_REQ    = 0x16,

    /* New — V9958 replies */
    PACKET_VDP_STATUS_REPLY     = 0x17,
    PACKET_VDP_DATA_REPLY       = 0x18,
    PACKET_PROTOCOL_ERROR       = 0x19,

    /* New — protocol operations */
    PACKET_COMMAND_STREAM       = 0x1A,
    PACKET_VRAM_UPLOAD_BEGIN    = 0x1B,
    PACKET_VRAM_UPLOAD_DATA     = 0x1C,
    PACKET_VRAM_UPLOAD_END      = 0x1D,
    PACKET_PACKET_RESET         = 0x1E,
} PacketType;

/** CURSOR_COMMAND payload byte 0 subcommands. */
typedef enum {
    CURSOR_ENABLE = 1,
    CURSOR_SHOW = 2,
    CURSOR_HIDE = 3,
    CURSOR_SET_POSITION = 4,
    CURSOR_MOVE_RELATIVE = 5,
    CURSOR_SET_STYLE = 6,
    CURSOR_SET_BLINK = 7,
    CURSOR_SET_COLOR = 8,
    CURSOR_SET_GEOMETRY = 9,
} CursorCommand;

/** Text cursor overlay styles. */
typedef enum {
    CURSOR_STYLE_BLOCK = 0,
    CURSOR_STYLE_UNDERLINE = 1,
    CURSOR_STYLE_LEFT_BAR = 2,
} CursorStyle;

/**
 * Decoded packet representation.
 *
 * length is the decoded payload byte count (0..MAX_PACKET_PAYLOAD).
 * The wire declared length (including the type byte) is length + 1.
 * No CRC, checksum, or integrity field is stored or transmitted.
 */
typedef struct {
    uint16_t length;
    uint8_t  type;
    uint8_t  payload[MAX_PACKET_PAYLOAD];
} Packet;

/**
 * Callback invoked for complete, CRC-valid packets.
 *
 * offset is the byte offset of the packet SYNC within the source stream.
 * userdata is caller-owned and passed through unchanged.
 */
typedef void (*PacketHandler)(const Packet *packet, size_t offset, void *userdata);

/** Return a stable debug name for a packet type value. */
const char *packet_type_name(uint8_t type);

/**
 * Encode a Packet into wire-format bytes.
 *
 * Writes SYNC0, SYNC1, 16-bit LE declared length (packet->length + 1),
 * type, and payload into output. No CRC is appended.
 *
 * Returns true on success. On failure (payload too large or output too small),
 * returns false and leaves *output_length unchanged.
 */
bool packet_encode(
    const Packet *packet,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

/* ==================================================================
 * Command stream opcodes (UOW-3)
 * ================================================================== */

typedef enum {
    OP_DATA_WRITE       = 0x01,
    OP_CTRL_WRITE       = 0x02,
    OP_PALETTE_WRITE    = 0x03,
    OP_INDIRECT_WRITE   = 0x04,
    OP_REG_BLOCK        = 0x10,
    OP_VRAM_ADDR_WRITE  = 0x11,
    OP_VRAM_SEQ_WRITE   = 0x12,
    OP_COMMAND_SETUP    = 0x20,
    OP_TEXT_RUN         = 0x30,
    OP_CELL_FILL        = 0x31,
    OP_CELL_COPY        = 0x32,
    OP_INSERT_LINES     = 0x33,
    OP_DELETE_LINES     = 0x34,
    OP_ERASE_EOL        = 0x35,
    OP_CLEAR_SCREEN     = 0x36,
    OP_SCROLL_REGION    = 0x37,
    OP_SCROLL_UP        = 0x38,
    OP_SET_CURSOR       = 0x40,
    OP_SET_ATTR         = 0x41,
    OP_SET_VRAM_ADDR    = 0x42,
    OP_SET_SCREEN_BASE  = 0x43,
    OP_SET_GLYPH_BASE   = 0x44,
    OP_SET_ATLAS_CONFIG = 0x46,
    OP_SET_DISP_OFFSET  = 0x47,
    OP_UPLOAD_BEGIN     = 0x50,
    OP_UPLOAD_DATA      = 0x51,
    OP_UPLOAD_END       = 0x52,
    OP_PRESENT          = 0xFE,
    OP_NOP              = 0xFF,
} StreamOpcode;

typedef struct {
    bool     known;
    bool     variable;
    uint16_t fixed_size;
} OpcodeDescriptor;

const OpcodeDescriptor *stream_opcode_descriptor(uint8_t opcode);

/* Text grid constants */
#define TEXT_COLS          80
#define TEXT_ROWS          24
#define GLYPH_WIDTH        6
#define GLYPH_HEIGHT       8
#define CELL_BYTES         3

/* Retained accelerator state */
typedef struct {
    uint8_t  cursor_col;
    uint8_t  cursor_row;
    uint8_t  foreground;
    uint8_t  background;
    bool     reverse;
    bool     cursor_wrap_pending;
    uint32_t vram_address;
    bool     vram_addr_pending;
    bool     screen_configured;
    bool     glyph_configured;
    bool     atlas_configured;
    uint32_t screen_base;
    uint32_t glyph_base;
    uint8_t  atlas_cols;
    uint8_t  display_offset;
} StreamState;

void stream_state_reset(StreamState *state);

/* Upload state machine */
typedef struct {
    bool     active;
    uint32_t base_address;
    uint32_t total_length;
    uint32_t next_offset;
    uint8_t  *shadow;
} UploadState;

void upload_state_reset(UploadState *upload);

/* Stream decoder result */
typedef struct {
    bool     accepted;
    bool     framebuffer_dirty;
    bool     presentation_requested;
    uint16_t ops_executed;
    uint16_t ops_skipped;
} StreamResult;

/**
 * Decode a command stream payload into expanded V9958 operations.
 *
 * Calls into the video device through callback functions for each
 * expanded operation. Updates stream_state and upload_state in place.
 */
bool stream_decode(
    const uint8_t *payload,
    uint16_t length,
    StreamState *state,
    UploadState *upload,
    StreamResult *result,
    /* callback context */
    void *device);

#endif
