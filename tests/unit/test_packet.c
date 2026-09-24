#include "../framework/test_framework.h"
#include "../../src/core/rel4u_packet.h"
#include <string.h>

static bool test_packet_encode_decode(void) {
    rel4u_header_t hdr;
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = REL4U_PKT_DATA;
    hdr.delivery_mode = 3; // REL4U_MODE_RELIABLE_ORDERED
    hdr.flags = REL4U_FLAG_ACK_IMMEDIATE;
    hdr.payload_len = 100;
    hdr.session_id = 0x12345678;
    hdr.seq_num = 42;
    hdr.ack_num = 40;
    hdr.sack_mask = 0x00000005; // Bits 0 and 2 set

    uint8_t buffer[64];
    int encoded_len = rel4u_packet_encode_header(&hdr, buffer, sizeof(buffer));
    ASSERT_EQ(encoded_len, REL4U_HEADER_LEN);

    rel4u_header_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    // Simulate total buffer with payload
    uint8_t full_packet[124];
    memcpy(full_packet, buffer, REL4U_HEADER_LEN);
    memset(full_packet + REL4U_HEADER_LEN, 'A', 100);

    int decode_res = rel4u_packet_decode_header(full_packet, sizeof(full_packet), &decoded);
    ASSERT_EQ(decode_res, 0);

    ASSERT_EQ(decoded.magic, REL4U_MAGIC);
    ASSERT_EQ(decoded.version, REL4U_VERSION);
    ASSERT_EQ(decoded.packet_type, REL4U_PKT_DATA);
    ASSERT_EQ(decoded.delivery_mode, 3);
    ASSERT_EQ(decoded.flags, REL4U_FLAG_ACK_IMMEDIATE);
    ASSERT_EQ(decoded.payload_len, 100);
    ASSERT_EQ(decoded.session_id, 0x12345678);
    ASSERT_EQ(decoded.seq_num, 42);
    ASSERT_EQ(decoded.ack_num, 40);
    ASSERT_EQ(decoded.sack_mask, 0x00000005);

    return true;
}

static bool test_packet_corrupt_rejection(void) {
    rel4u_header_t hdr;
    hdr.magic = 0x9999; // Invalid magic
    hdr.version = REL4U_VERSION;
    hdr.packet_type = REL4U_PKT_DATA;
    hdr.delivery_mode = 0;
    hdr.flags = 0;
    hdr.payload_len = 10;
    hdr.session_id = 1;
    hdr.seq_num = 1;
    hdr.ack_num = 0;
    hdr.sack_mask = 0;

    uint8_t buffer[64];
    rel4u_packet_encode_header(&hdr, buffer, sizeof(buffer));

    rel4u_header_t decoded;
    int res = rel4u_packet_decode_header(buffer, 34, &decoded);
    ASSERT_EQ(res, -2); // Invalid magic error

    return true;
}

static bool test_packet_reset_codec(void) {
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = REL4U_PKT_RESET;
    hdr.session_id = 0xDEADBEEF;
    hdr.seq_num = 999;

    uint8_t buffer[REL4U_HEADER_LEN];
    int encoded = rel4u_packet_encode_header(&hdr, buffer, sizeof(buffer));
    ASSERT_EQ(encoded, REL4U_HEADER_LEN);

    rel4u_header_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    int res = rel4u_packet_decode_header(buffer, sizeof(buffer), &decoded);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(decoded.packet_type, REL4U_PKT_RESET);
    ASSERT_EQ(decoded.session_id, 0xDEADBEEF);
    ASSERT_EQ(decoded.seq_num, 999);

    return true;
}

static bool test_packet_codec_errors(void) {
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = REL4U_PKT_DATA;
    hdr.payload_len = 50;

    uint8_t buffer[128];
    // Encode buffer too small
    ASSERT_EQ(rel4u_packet_encode_header(&hdr, buffer, REL4U_HEADER_LEN - 1), -1);
    // NULL encode args
    ASSERT_EQ(rel4u_packet_encode_header(NULL, buffer, sizeof(buffer)), -1);
    ASSERT_EQ(rel4u_packet_encode_header(&hdr, NULL, sizeof(buffer)), -1);

    // Encode valid packet
    ASSERT_EQ(rel4u_packet_encode_header(&hdr, buffer, sizeof(buffer)), REL4U_HEADER_LEN);

    rel4u_header_t decoded;
    // Decode buffer too small (< REL4U_HEADER_LEN)
    ASSERT_EQ(rel4u_packet_decode_header(buffer, REL4U_HEADER_LEN - 1, &decoded), -1);
    // NULL decode args
    ASSERT_EQ(rel4u_packet_decode_header(NULL, sizeof(buffer), &decoded), -1);
    ASSERT_EQ(rel4u_packet_decode_header(buffer, sizeof(buffer), NULL), -1);

    // Incomplete payload: declared 50 bytes, but only 20 provided after header
    ASSERT_EQ(rel4u_packet_decode_header(buffer, REL4U_HEADER_LEN + 20, &decoded), -4);

    // Invalid version
    hdr.version = 99;
    rel4u_packet_encode_header(&hdr, buffer, sizeof(buffer));
    ASSERT_EQ(rel4u_packet_decode_header(buffer, REL4U_HEADER_LEN + 50, &decoded), -3);

    return true;
}

TEST_SUITE_BEGIN("Packet Codec Unit Tests")
    RUN_TEST(test_packet_encode_decode);
    RUN_TEST(test_packet_reset_codec);
    RUN_TEST(test_packet_corrupt_rejection);
    RUN_TEST(test_packet_codec_errors);
TEST_SUITE_END()
