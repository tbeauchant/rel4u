#include "../framework/test_framework.h"
#include "../../src/core/rel4u_window.h"
#include "../../src/core/rel4u_mpmc.h"

static bool test_sequence_math(void) {
    ASSERT_TRUE(rel4u_seq_lt(1, 2));
    ASSERT_TRUE(rel4u_seq_gt(2, 1));
    ASSERT_TRUE(rel4u_seq_lte(2, 2));
    ASSERT_TRUE(rel4u_seq_gte(2, 2));

    // Wrapping around 2^32 - 1
    uint32_t a = 0xFFFFFFFFU;
    uint32_t b = 1U;
    ASSERT_TRUE(rel4u_seq_lt(a, b));
    ASSERT_TRUE(rel4u_seq_gt(b, a));
    ASSERT_EQ(rel4u_seq_diff(b, a), 2);

    return true;
}

static bool test_send_window_and_sack(void) {
    rel4u_send_window_t win;
    ASSERT_EQ(rel4u_send_window_init(&win, 16, 100), 0);

    ASSERT_TRUE(rel4u_send_window_can_send(&win));

    // Push 4 packets (seq 100, 101, 102, 103)
    const char* msg = "test_payload";
    uint32_t seq;
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_ORDERED, msg, strlen(msg), 1000, &seq), 0);
        ASSERT_EQ(seq, 100 + (uint32_t)i);
    }
    ASSERT_EQ(win.in_flight, 4);

    // Simulate SACK acknowledgment: ack_num = 100 (100 received), sack_mask bit 1 set (102 received)
    // Means 100 and 102 received, 101 still unacked!
    uint32_t sack_mask = (1U << 1); // 100 + 1 + 1 = 102
    rel4u_rtt_t rtt;
    rel4u_rtt_init(&rtt, 200, 20, 2000);
    rel4u_send_window_on_ack(&win, 100, sack_mask, 1500000000ULL, &rtt, NULL);

    ASSERT_EQ(win.head_seq, 101);
    ASSERT_EQ(win.in_flight, 2); // 101 and 103 remain unacked

    // Check expired retransmissions
    rel4u_send_slot_t* expired[4];
    size_t exp_count = rel4u_send_window_get_expired(&win, 2000000000ULL, 100, expired, 4);
    ASSERT_EQ(exp_count, 2); // 101 and 103
    ASSERT_EQ(expired[0]->seq_num, 101);
    ASSERT_EQ(expired[1]->seq_num, 103);

    rel4u_send_window_destroy(&win);
    return true;
}

static bool test_recv_window_reassembly_and_sack(void) {
    rel4u_recv_window_t win;
    ASSERT_EQ(rel4u_recv_window_init(&win, 16, 100), 0);

    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.delivery_mode = REL4U_MODE_RELIABLE_ORDERED;

    rel4u_recv_slot_t ready[16];
    size_t ready_count = 0;

    // 1. Arrive out of order: packet 102 arrives first
    hdr.seq_num = 102;
    hdr.payload_len = 5;
    const uint8_t* p102 = (const uint8_t*)"102--";
    int res = rel4u_recv_window_on_packet(&win, &hdr, p102, ready, 16, &ready_count);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(ready_count, 0); // Not ready yet because 100 and 101 are missing!

    // Check SACK info generated
    uint32_t ack_num, sack_mask;
    rel4u_recv_window_get_ack_info(&win, &ack_num, &sack_mask);
    ASSERT_EQ(ack_num, 99);
    ASSERT_EQ(sack_mask, (1U << 2)); // Bit 2 (100 + 2 = 102) is set!

    // 2. Packet 101 arrives
    hdr.seq_num = 101;
    hdr.payload_len = 5;
    const uint8_t* p101 = (const uint8_t*)"101--";
    res = rel4u_recv_window_on_packet(&win, &hdr, p101, ready, 16, &ready_count);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(ready_count, 0); // Still waiting for 100!

    rel4u_recv_window_get_ack_info(&win, &ack_num, &sack_mask);
    ASSERT_EQ(ack_num, 99);
    ASSERT_EQ(sack_mask, (1U << 1) | (1U << 2)); // Bits 1 and 2 set (101, 102)

    // 3. Packet 100 arrives (fills the hole!)
    hdr.seq_num = 100;
    hdr.payload_len = 5;
    const uint8_t* p100 = (const uint8_t*)"100--";
    res = rel4u_recv_window_on_packet(&win, &hdr, p100, ready, 16, &ready_count);
    ASSERT_EQ(res, 0);
    ASSERT_EQ(ready_count, 3); // All 3 packets (100, 101, 102) released in contiguous order!

    ASSERT_EQ(ready[0].seq_num, 100);
    ASSERT_EQ(ready[1].seq_num, 101);
    ASSERT_EQ(ready[2].seq_num, 102);

    rel4u_recv_window_get_ack_info(&win, &ack_num, &sack_mask);
    ASSERT_EQ(ack_num, 102);
    ASSERT_EQ(sack_mask, 0);

    rel4u_recv_window_destroy(&win);
    return true;
}

static bool test_unreliable_ordered_mode(void) {
    rel4u_recv_window_t win;
    ASSERT_EQ(rel4u_recv_window_init(&win, 16, 1), 0);

    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.delivery_mode = REL4U_MODE_UNRELIABLE_ORDERED;

    rel4u_recv_slot_t ready[1];
    size_t ready_count = 0;

    // Send seq 5
    hdr.seq_num = 5;
    hdr.payload_len = 3;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"msg", ready, 1, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 5);

    // Send seq 3 (older than 5) -> should be dropped!
    hdr.seq_num = 3;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"msg", ready, 1, &ready_count), 1);
    ASSERT_EQ(ready_count, 0);

    // Send seq 6 (newer than 5) -> should be accepted!
    hdr.seq_num = 6;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"msg", ready, 1, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 6);

    rel4u_recv_window_destroy(&win);
    return true;
}

static bool test_window_reset_and_reclaim(void) {
    rel4u_send_window_t win;
    ASSERT_EQ(rel4u_send_window_init(&win, 16, 1), 0);

    rel4u_mpmc_queue_t queue;
    ASSERT_EQ(rel4u_mpmc_init(&queue, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 16, 0), 0);

    uint32_t seq;
    ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_ORDERED, "pkt1", 4, 1000, &seq), 0);
    ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_UNRELIABLE_UNORDERED, "pkt2", 4, 1000, &seq), 0);
    ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_UNORDERED, "pkt3", 4, 1000, &seq), 0);

    // Reclaim unacked reliable items (pkt1 and pkt3; pkt2 unreliable should be skipped)
    size_t count = rel4u_send_window_reclaim_unacked(&win, &queue);
    ASSERT_EQ(count, 2);

    rel4u_mpmc_item_t item1, item2;
    ASSERT_TRUE(rel4u_mpmc_try_pop(&queue, &item1));
    ASSERT_EQ(item1.mode, REL4U_MODE_RELIABLE_ORDERED);
    ASSERT_EQ(memcmp(item1.data, "pkt1", 4), 0);

    ASSERT_TRUE(rel4u_mpmc_try_pop(&queue, &item2));
    ASSERT_EQ(item2.mode, REL4U_MODE_RELIABLE_UNORDERED);
    ASSERT_EQ(memcmp(item2.data, "pkt3", 4), 0);

    // Reset window
    ASSERT_EQ(rel4u_send_window_reset(&win, 1), 0);
    ASSERT_EQ(win.in_flight, 0);
    ASSERT_EQ(win.head_seq, 1);
    ASSERT_EQ(win.next_seq, 1);

    rel4u_send_window_destroy(&win);
    rel4u_mpmc_destroy(&queue);
    return true;
}

TEST_SUITE_BEGIN("Sliding Window & SACK Unit Tests")
    RUN_TEST(test_sequence_math);
    RUN_TEST(test_send_window_and_sack);
    RUN_TEST(test_recv_window_reassembly_and_sack);
    RUN_TEST(test_unreliable_ordered_mode);
    RUN_TEST(test_window_reset_and_reclaim);
TEST_SUITE_END()
