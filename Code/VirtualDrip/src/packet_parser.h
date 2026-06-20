#ifndef PACKET_PARSER_H
#define PACKET_PARSER_H

/**
 * @file packet_parser.h
 * Streaming byte-oriented Virtual Drip packet decoder.
 *
 * The parser accepts bytes from serial input, file replay, or tests. It finds
 * PACKET_SYNC0/PACKET_SYNC1, reads the 16-bit little-endian declared length,
 * TYPE, and PAYLOAD, and emits complete valid packets through PacketHandler.
 * It owns decoding state only; packet interpretation belongs to higher layers
 * such as packet dispatch and video backends.
 *
 * There is no CRC or integrity check. Framing errors (invalid declared length)
 * are logged to stderr and the parser returns to synchronization search.
 */

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Current byte expected by the streaming parser state machine. */
typedef enum {
    /** Ignore input until PACKET_SYNC0 is seen. */
    PACKET_PARSER_WAIT_SYNC0,
    /** Read the second sync byte. */
    PACKET_PARSER_WAIT_SYNC1,
    /** Read low byte of 16-bit declared length (LE). */
    PACKET_PARSER_READ_LEN_LO,
    /** Read high byte of 16-bit declared length (LE) and validate. */
    PACKET_PARSER_READ_LEN_HI,
    /** Read TYPE. */
    PACKET_PARSER_READ_TYPE,
    /** Read the decoded payload bytes. */
    PACKET_PARSER_READ_PAYLOAD,
} PacketParserState;

/**
 * Stateful packet decoder.
 *
 * offset tracks all bytes fed to the parser. packet_offset records the SYNC
 * offset of the packet currently being assembled. Invalid declared lengths
 * are logged to stderr; the parser then returns to WAIT_SYNC0.
 */
typedef struct {
    /** Current point in the packet framing state machine. */
    PacketParserState state;
    /** Packet currently being assembled. Valid only after dispatch. */
    Packet packet;
    /** Raw 16-bit declared length from wire (includes type byte). */
    uint16_t declared_length;
    /** Number of payload bytes already copied into packet.payload. */
    uint16_t payload_index;
    /** Absolute byte offset of the next byte to consume. */
    size_t offset;
    /** Absolute byte offset where the current packet's SYNC was seen. */
    size_t packet_offset;
    /** Count of valid packets emitted. */
    size_t packet_count;
    /** Callback for valid complete packets. May be NULL. */
    PacketHandler handler;
    /** Caller-owned context passed to handler. */
    void *userdata;
} PacketParser;

/** Initialize a parser and register the callback for valid packets. */
void packet_parser_init(PacketParser *parser, PacketHandler handler, void *userdata);

/** Feed one byte into the parser; may synchronously invoke the callback. */
void packet_parser_feed(PacketParser *parser, uint8_t value);

/** Return true when EOF would leave a truncated packet. */
bool packet_parser_has_partial_packet(const PacketParser *parser);

/** Number of valid packets emitted through the callback. */
size_t packet_parser_packet_count(const PacketParser *parser);

#endif
