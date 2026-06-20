#include "packet_parser.h"

#include <stdio.h>
#include <string.h>

/*
 * This decoder is intentionally policy-free. It does not know whether bytes
 * came from a serial fd, a file, or a test harness, and it does not know about
 * the selected video backend. That keeps malformed-stream recovery identical
 * across all packet sources.
 *
 * Frame format after sync: [LEN_LO][LEN_HI][TYPE][PAYLOAD...]
 * Declared length (LE) counts TYPE + PAYLOAD bytes (1..1025).
 * Payload length = declared_length - 1.
 */

void packet_parser_init(PacketParser *parser, PacketHandler handler, void *userdata)
{
    memset(parser, 0, sizeof(*parser));
    parser->state = PACKET_PARSER_WAIT_SYNC0;
    parser->handler = handler;
    parser->userdata = userdata;
}

void packet_parser_feed(PacketParser *parser, uint8_t value)
{
    size_t current_offset = parser->offset++;

    switch (parser->state) {
    case PACKET_PARSER_WAIT_SYNC0:
        if (value == PACKET_SYNC0) {
            parser->packet_offset = current_offset;
            parser->state = PACKET_PARSER_WAIT_SYNC1;
        }
        break;

    case PACKET_PARSER_WAIT_SYNC1:
        if (value == PACKET_SYNC1) {
            parser->state = PACKET_PARSER_READ_LEN_LO;
        } else if (value == PACKET_SYNC0) {
            parser->packet_offset = current_offset;
        } else {
            parser->state = PACKET_PARSER_WAIT_SYNC0;
        }
        break;

    case PACKET_PARSER_READ_LEN_LO:
        parser->declared_length = value;
        parser->state = PACKET_PARSER_READ_LEN_HI;
        break;

    case PACKET_PARSER_READ_LEN_HI:
        parser->declared_length |= (uint16_t)(value << 8);
        if (parser->declared_length < PACKET_MIN_DECLARED_LENGTH ||
            parser->declared_length > PACKET_MAX_DECLARED_LENGTH) {
            fprintf(stderr,
                "Packet invalid declared length at offset %zu: %u\n",
                parser->packet_offset,
                parser->declared_length);
            parser->state = PACKET_PARSER_WAIT_SYNC0;
            break;
        }
        parser->packet.length = (uint16_t)(parser->declared_length - 1u);
        parser->payload_index = 0;
        parser->state = PACKET_PARSER_READ_TYPE;
        break;

    case PACKET_PARSER_READ_TYPE:
        parser->packet.type = value;
        if (parser->packet.length == 0) {
            /* type-only packet, dispatch immediately */
            ++parser->packet_count;
            if (parser->handler != NULL) {
                parser->handler(&parser->packet, parser->packet_offset, parser->userdata);
            }
            parser->state = PACKET_PARSER_WAIT_SYNC0;
        } else {
            parser->state = PACKET_PARSER_READ_PAYLOAD;
        }
        break;

    case PACKET_PARSER_READ_PAYLOAD:
        parser->packet.payload[parser->payload_index++] = value;
        if (parser->payload_index == parser->packet.length) {
            ++parser->packet_count;
            if (parser->handler != NULL) {
                parser->handler(&parser->packet, parser->packet_offset, parser->userdata);
            }
            parser->state = PACKET_PARSER_WAIT_SYNC0;
        }
        break;
    }
}

bool packet_parser_has_partial_packet(const PacketParser *parser)
{
    return parser->state != PACKET_PARSER_WAIT_SYNC0;
}

size_t packet_parser_packet_count(const PacketParser *parser)
{
    return parser->packet_count;
}
