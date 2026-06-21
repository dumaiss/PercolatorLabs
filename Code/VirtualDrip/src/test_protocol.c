/*
 * Built-in protocol smoke tests for Virtual Drip.
 *
 * Linked into virtual-vdp and activated by --test. Uses assert() so tests
 * abort with a clear message on failure.
 */

#include "protocol.h"
#include "packet_parser.h"
#include "video_device.h"
#include "video_device_vdrip9958.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Test helper: feed bytes, collect the last decoded packet.
 * ------------------------------------------------------------------ */
static Packet last_packet;
static bool last_packet_received;

static void test_handler(const Packet *packet, size_t offset, void *userdata)
{
    (void)offset;
    (void)userdata;
    memcpy(&last_packet, packet, sizeof(last_packet));
    last_packet_received = true;
}

static void feed_bytes(PacketParser *parser, const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        packet_parser_feed(parser, bytes[i]);
    }
}

/* ------------------------------------------------------------------
 * Test 1: encode/decode round-trip at boundary payload lengths.
 * ------------------------------------------------------------------ */
static int test_round_trip(void)
{
    static const uint16_t test_lengths[] = {0, 1, 255, 256, 1023, 1024};
    static const size_t num_lengths = sizeof(test_lengths) / sizeof(test_lengths[0]);

    for (size_t i = 0; i < num_lengths; ++i) {
        uint16_t len = test_lengths[i];

        Packet sent;
        sent.length = len;
        sent.type = (uint8_t)(0x01 + i);  /* vary type per test */
        for (uint16_t j = 0; j < len; ++j) {
            sent.payload[j] = (uint8_t)(j & 0xFF);
        }

        uint8_t wire[PACKET_MAX_WIRE_SIZE];
        size_t wire_len = 0;
        assert(packet_encode(&sent, wire, sizeof(wire), &wire_len));
        assert(wire_len == PACKET_SYNC_SIZE + 2u + 1u + len);

        PacketParser parser;
        last_packet_received = false;
        packet_parser_init(&parser, test_handler, NULL);
        feed_bytes(&parser, wire, wire_len);

        assert(last_packet_received);
        assert(last_packet.length == len);
        assert(last_packet.type == sent.type);
        assert(memcmp(last_packet.payload, sent.payload, len) == 0);
    }

    printf("  test_round_trip: PASS (%zu lengths)\n", num_lengths);
    return 0;
}

/* ------------------------------------------------------------------
 * Test 2: parser resynchronization after invalid declared length.
 * ------------------------------------------------------------------ */
static int test_parser_resync(void)
{
    /* Valid packet: type=0x10, 3 payload bytes */
    Packet sent;
    sent.length = 3;
    sent.type = 0x10;
    sent.payload[0] = 0xAA;
    sent.payload[1] = 0xBB;
    sent.payload[2] = 0xCC;

    uint8_t good[PACKET_MAX_WIRE_SIZE];
    size_t good_len = 0;
    assert(packet_encode(&sent, good, sizeof(good), &good_len));

    /* Build a stream: valid packet, garbage with invalid length, valid packet */
    uint8_t stream[2048];
    size_t pos = 0;
    memcpy(&stream[pos], good, good_len);
    pos += good_len;

    /* Inject a fake frame: A5 5A with declared length = 2000 (invalid > 1025) */
    stream[pos++] = PACKET_SYNC0;
    stream[pos++] = PACKET_SYNC1;
    stream[pos++] = 0xD0;  /* 2000 & 0xFF */
    stream[pos++] = 0x07;  /* 2000 >> 8 */
    /* The parser should reject this and return to sync search */
    /* Add some garbage bytes after the invalid length */
    for (int k = 0; k < 5; ++k) stream[pos++] = 0xFF;

    /* Then a second valid packet (different type) */
    sent.type = 0x11;
    sent.payload[0] = 0xDD;
    assert(packet_encode(&sent, good, sizeof(good), &good_len));
    memcpy(&stream[pos], good, good_len);
    pos += good_len;

    PacketParser parser;
    last_packet_received = false;
    packet_parser_init(&parser, test_handler, NULL);
    feed_bytes(&parser, stream, pos);

    /* The first valid packet should have been dispatched,
     * then the invalid frame rejected, then the second valid
     * packet dispatched. The parser count reflects both. */
    assert(packet_parser_packet_count(&parser) == 2);
    /* The last packet captured by the handler is the second valid one. */
    assert(last_packet_received);
    assert(last_packet.type == 0x11);

    /* The second valid packet should arrive next */
    last_packet_received = false;
    /* The parser already processed the whole stream synchronously */
    /* Reset and feed only the portion after the invalid frame? */
    /* Actually, the stream was fed in one go. The parser should have
     * dispatched the first good packet, then rejected the invalid frame,
     * then resynchronized and dispatched the second good packet.
     * But our test_handler only captures the LAST packet.
     * Let's verify the parser count instead. */
    assert(packet_parser_packet_count(&parser) == 2);

    printf("  test_parser_resync: PASS (packet_count=%zu)\n",
           packet_parser_packet_count(&parser));
    return 0;
}

/* ------------------------------------------------------------------
 * Test 3: back-to-back valid frames.
 * ------------------------------------------------------------------ */
static int test_back_to_back(void)
{
    Packet p1;
    p1.length = 2;
    p1.type = 0x20;
    p1.payload[0] = 0x11;
    p1.payload[1] = 0x22;

    Packet p2;
    p2.length = 1;
    p2.type = 0x21;
    p2.payload[0] = 0x33;

    uint8_t w1[PACKET_MAX_WIRE_SIZE], w2[PACKET_MAX_WIRE_SIZE];
    size_t len1 = 0, len2 = 0;
    assert(packet_encode(&p1, w1, sizeof(w1), &len1));
    assert(packet_encode(&p2, w2, sizeof(w2), &len2));

    uint8_t stream[2048];
    memcpy(stream, w1, len1);
    memcpy(&stream[len1], w2, len2);

    PacketParser parser;
    packet_parser_init(&parser, test_handler, NULL);
    feed_bytes(&parser, stream, len1 + len2);

    assert(packet_parser_packet_count(&parser) == 2);
    assert(last_packet_received);
    assert(last_packet.type == 0x21);

    printf("  test_back_to_back: PASS\n");
    return 0;
}

/* ------------------------------------------------------------------
 * Test 4: type-only (zero-payload) packet.
 * ------------------------------------------------------------------ */
static int test_type_only(void)
{
    Packet sent;
    sent.length = 0;
    sent.type = PACKET_PING;

    uint8_t wire[PACKET_MAX_WIRE_SIZE];
    size_t wire_len = 0;
    assert(packet_encode(&sent, wire, sizeof(wire), &wire_len));
    /* Expected: SYNC(2) + LEN(2) + TYPE(1) = 5 bytes, LEN = 1 */
    assert(wire_len == PACKET_SYNC_SIZE + 2u + 1u);

    PacketParser parser;
    last_packet_received = false;
    packet_parser_init(&parser, test_handler, NULL);
    feed_bytes(&parser, wire, wire_len);

    assert(last_packet_received);
    assert(last_packet.length == 0);
    assert(last_packet.type == PACKET_PING);

    printf("  test_type_only: PASS\n");
    return 0;
}

/* ------------------------------------------------------------------
 * Test 5: stream decode — basic opcodes.
 * ------------------------------------------------------------------ */
static int test_stream_decode(void)
{
    /* Build a command stream: SET_CURSOR(10,5) + SET_ATTR(15,0,0) + DATA_WRITE(0x42) + PRESENT */
    uint8_t stream[32];
    uint16_t pos = 0;
    stream[pos++] = OP_SET_CURSOR;  stream[pos++] = 10; stream[pos++] = 5;
    stream[pos++] = OP_SET_ATTR;    stream[pos++] = 15; stream[pos++] = 0; stream[pos++] = 0;
    stream[pos++] = OP_DATA_WRITE;  stream[pos++] = 0x42;
    stream[pos++] = OP_PRESENT;

    StreamState state = {0};
    UploadState upload = {0};
    StreamResult result;
    stream_state_reset(&state);
    upload_state_reset(&upload);

    bool ok = stream_decode(stream, pos, &state, &upload, &result, NULL);
    assert(ok);
    assert(result.ops_executed == 4);
    assert(result.ops_skipped == 0);
    assert(result.presentation_requested);
    assert(result.framebuffer_dirty);
    assert(state.cursor_col == 10);
    assert(state.cursor_row == 5);
    assert(state.foreground == 15);
    assert(state.background == 0);
    assert(!state.reverse);

    /* Unknown opcode stops stream */
    stream[0] = 0xAB;
    stream_state_reset(&state);
    ok = stream_decode(stream, pos, &state, &upload, &result, NULL);
    assert(result.ops_executed == 0);
    (void)ok;

    /* NOP is harmless */
    uint8_t nop_stream[] = { OP_NOP, OP_NOP, OP_PRESENT };
    stream_state_reset(&state);
    ok = stream_decode(nop_stream, sizeof(nop_stream), &state, &upload, &result, NULL);
    assert(result.ops_executed == 3);
    assert(result.presentation_requested);
    (void)ok;

    upload_state_reset(&upload);
    printf("  test_stream_decode: PASS\n");
    return 0;
}

/* ------------------------------------------------------------------
 * Test 6: upload state machine.
 * ------------------------------------------------------------------ */
static int test_upload(void)
{
    uint8_t stream[256];
    uint16_t pos = 0;

    /* Begin: addr=0x1000, total=6 */
    stream[pos++] = OP_UPLOAD_BEGIN;
    stream[pos++] = 0x00; stream[pos++] = 0x10; stream[pos++] = 0x00; /* addr */
    stream[pos++] = 6; stream[pos++] = 0; stream[pos++] = 0;          /* total */
    stream[pos++] = 0;                                                  /* flags */

    /* Data: offset=0, length=3, data=ABC */
    stream[pos++] = OP_UPLOAD_DATA;
    stream[pos++] = 0; stream[pos++] = 0; stream[pos++] = 0;           /* offset */
    stream[pos++] = 3; stream[pos++] = 0;                              /* length */
    stream[pos++] = 'A'; stream[pos++] = 'B'; stream[pos++] = 'C';

    /* Data: offset=3, length=3, data=DEF */
    stream[pos++] = OP_UPLOAD_DATA;
    stream[pos++] = 3; stream[pos++] = 0; stream[pos++] = 0;
    stream[pos++] = 3; stream[pos++] = 0;
    stream[pos++] = 'D'; stream[pos++] = 'E'; stream[pos++] = 'F';

    /* End */
    stream[pos++] = OP_UPLOAD_END;

    StreamState state = {0};
    UploadState upload = {0};
    StreamResult result;
    stream_state_reset(&state);
    upload_state_reset(&upload);

    bool ok = stream_decode(stream, pos, &state, &upload, &result, NULL);
    assert(ok);
    assert(result.ops_executed == 4); /* begin + data + data + end */
    assert(!upload.active);

    /* Duplicate chunk: build fresh streams */
    {
        uint8_t begin[8] = { OP_UPLOAD_BEGIN, 0,0,0, 3,0,0, 0 };
        uint8_t data1[9] = { OP_UPLOAD_DATA, 0,0,0, 3,0, 'A','B','C' };
        uint8_t data2[9] = { OP_UPLOAD_DATA, 0,0,0, 3,0, 'X','Y','Z' };

        stream_state_reset(&state);
        upload_state_reset(&upload);
        assert(stream_decode(begin, sizeof(begin), &state, &upload, &result, NULL));
        assert(upload.active);
        assert(stream_decode(data1, sizeof(data1), &state, &upload, &result, NULL));
        assert(upload.next_offset == 3);
        /* Exact duplicate */
        assert(stream_decode(data1, sizeof(data1), &state, &upload, &result, NULL));
        assert(upload.next_offset == 3); /* unchanged */
        /* Different data at same offset → rejected */
        bool rejected = stream_decode(data2, sizeof(data2), &state, &upload, &result, NULL);
        /* stream stops, upload stays active */
        assert(upload.active);
        (void)rejected;
    }

    upload_state_reset(&upload);
    printf("  test_upload: PASS\n");
    return 0;
}

static int test_v9958_cell_accelerator(void)
{
    VideoDevice *device = video_device_vdrip9958_create();
    assert(device != NULL);

    StreamState state;
    stream_state_reset(&state);
    state.screen_configured = true;
    state.glyph_configured = true;
    state.atlas_configured = true;
    state.screen_base = 0x0D400;
    state.glyph_base = 0x10000;
    state.atlas_cols = 1;

    /* G6, display on, page zero, sprite tables in high VRAM,
     * 212-line interlace. */
    const uint8_t regs[] = {
        0, 12,
        0x0A, 0x40, 0, 0, 0, 0xE4, 0x3F, 0x04, 0, 0x88, 0, 0x03
    };
    assert(video_device_vdrip9958_stream_op(
        device, OP_REG_BLOCK, regs, sizeof(regs), &state));

    /* Glyph zero: solid 6x8 mask at atlas origin. */
    for (uint8_t row = 0; row < 8; ++row) {
        uint32_t address = 0x10000u + (uint32_t)row * 256u;
        uint8_t upload[] = {
            (uint8_t)address, (uint8_t)(address >> 8), (uint8_t)(address >> 16),
            3, 0xFF, 0xFF, 0xFF
        };
        assert(video_device_vdrip9958_stream_op(
            device, OP_VRAM_ADDR_WRITE, upload, sizeof(upload), &state));
    }

    state.background = 4;
    const uint8_t run[] = { 1, 0, 0, 15, 4, 0, 1, 0 };
    assert(video_device_vdrip9958_stream_op(
        device, OP_TEXT_RUN, run, sizeof(run), &state));

    /* Dirty the normally-unused final G6 source line, then verify that a
     * terminal clear also clears the complete visible page margins. */
    const uint32_t bottom_address = 211u * 256u;
    const uint8_t dirty_bottom[] = {
        (uint8_t)bottom_address,
        (uint8_t)(bottom_address >> 8),
        (uint8_t)(bottom_address >> 16),
        1, 0xFF
    };
    assert(video_device_vdrip9958_stream_op(
        device, OP_VRAM_ADDR_WRITE,
        dirty_bottom, sizeof(dirty_bottom), &state));
    assert(video_device_vdrip9958_stream_op(
        device, OP_CLEAR_SCREEN, NULL, 0, &state));

    /* Install the BIOS-style 6x8 block sprite at row 0, column 1. */
    const uint8_t cursor_pattern[] = {
        0x00, 0xF8, 0x01, 8,
        0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0
    };
    const uint8_t cursor_colors[] = {
        0x00, 0xF0, 0x01, 16,
        0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
        0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F
    };
    uint8_t cursor_sat[] = {
        0x00, 0xF2, 0x01, 8,
        7, 3, 0, 0, 0xD8, 0, 0, 0
    };
    assert(video_device_vdrip9958_stream_op(
        device, OP_VRAM_ADDR_WRITE,
        cursor_pattern, sizeof(cursor_pattern), &state));
    assert(video_device_vdrip9958_stream_op(
        device, OP_VRAM_ADDR_WRITE,
        cursor_colors, sizeof(cursor_colors), &state));
    assert(video_device_vdrip9958_stream_op(
        device, OP_VRAM_ADDR_WRITE, cursor_sat, sizeof(cursor_sat), &state));

    uint32_t *frame = calloc(512u * 424u, sizeof(*frame));
    assert(frame != NULL);
    assert(video_device_render_framebuffer(device, frame, 512, 424));
    const uint32_t margin_color = frame[400u * 512u];
    assert(margin_color != 0);
    for (uint16_t y = 400; y < 424; ++y) {
        for (uint16_t x = 0; x < 512; ++x) {
            assert(frame[(uint32_t)y * 512u + x] == margin_color);
        }
    }
    assert(frame[16u * 512u + 6u] != frame[16u * 512u + 100u]);

    /* Move the SAT entry and verify the old position is erased and the new
     * position is visible after the next render. */
    cursor_sat[5] = 6;
    assert(video_device_vdrip9958_stream_op(
        device, OP_VRAM_ADDR_WRITE, cursor_sat, sizeof(cursor_sat), &state));
    assert(video_device_render_framebuffer(device, frame, 512, 424));
    assert(frame[16u * 512u + 6u] == frame[16u * 512u + 100u]);
    assert(frame[16u * 512u + 12u] != frame[16u * 512u + 100u]);

    free(frame);
    video_device_destroy(device);
    printf("  test_v9958_cell_accelerator: PASS\n");
    return 0;
}

/* ------------------------------------------------------------------
 * Test entry point.
 * ------------------------------------------------------------------ */
int test_protocol(void)
{
    printf("Virtual Drip protocol tests:\n");

    int failures = 0;

    failures += test_round_trip();
    failures += test_parser_resync();
    failures += test_back_to_back();
    failures += test_type_only();
    failures += test_stream_decode();
    failures += test_upload();
    failures += test_v9958_cell_accelerator();

    if (failures == 0) {
        printf("All protocol tests passed.\n");
        return 0;
    }

    fprintf(stderr, "%d test(s) failed.\n", failures);
    return 1;
}
