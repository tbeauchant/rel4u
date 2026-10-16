#include "../framework/test_framework.h"
#include "../../src/core/rel4u_window.h"
#include "../../src/core/rel4u_mpmc.h"

#include "../framework/test_resources.h"

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

static bool test_send_window_full_and_exhaustion(void) {
    rel4u_send_window_t win;
    ASSERT_EQ(rel4u_send_window_init(&win, 4, 10), 0);

    ASSERT_TRUE(rel4u_send_window_can_send(&win));

    uint32_t seq;
    // Push 4 reliable packets to saturate capacity
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_ORDERED, "data", 4, 1000, &seq), 0);
        ASSERT_EQ(seq, 10 + (uint32_t)i);
    }

    // Window must be full
    ASSERT_FALSE(rel4u_send_window_can_send(&win));

    // 5th reliable push must fail with -2 (window full)
    ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_ORDERED, "data", 4, 1000, &seq), -2);

    // Unreliable packet does not consume window slot and should succeed
    ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_UNRELIABLE_UNORDERED, "unrel", 5, 1000, &seq), 0);

    // ACK packet 10 to free 1 slot
    rel4u_send_window_on_ack(&win, 10, 0, 2000, NULL, NULL);
    ASSERT_EQ(win.in_flight, 3);
    ASSERT_TRUE(rel4u_send_window_can_send(&win));

    // Now pushing reliable packet succeeds
    ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_ORDERED, "data", 4, 2000, &seq), 0);
    ASSERT_EQ(seq, 14);

    rel4u_send_window_destroy(&win);
    return true;
}

static bool test_recv_window_duplicate_and_capacity(void) {
    rel4u_recv_window_t win;
    ASSERT_EQ(rel4u_recv_window_init(&win, 8, 50), 0);

    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.delivery_mode = REL4U_MODE_RELIABLE_ORDERED;
    hdr.payload_len = 4;

    rel4u_recv_slot_t ready[8];
    size_t ready_count = 0;

    // 1. In-order packet 50
    hdr.seq_num = 50;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"p050", ready, 8, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(win.expected_seq, 51);

    // 2. Duplicate of already delivered packet 50 -> must return 1 (drop)
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"p050", ready, 8, &ready_count), 1);
    ASSERT_EQ(ready_count, 0);

    // 3. Out-of-order packet 53 (within capacity 8, expected is 51, diff 2)
    hdr.seq_num = 53;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"p053", ready, 8, &ready_count), 0);
    ASSERT_EQ(ready_count, 0);

    // 4. Duplicate of buffered out-of-order packet 53 -> must return 1 (drop)
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"p053", ready, 8, &ready_count), 1);

    // 5. Packet beyond capacity (expected is 51, packet is 60: diff = 9 >= capacity 8)
    hdr.seq_num = 60;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"p060", ready, 8, &ready_count), -2);

    rel4u_recv_window_destroy(&win);
    return true;
}

static bool test_recv_window_reliable_unordered(void) {
    rel4u_recv_window_t win;
    ASSERT_EQ(rel4u_recv_window_init(&win, 16, 10), 0);

    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.delivery_mode = REL4U_MODE_RELIABLE_UNORDERED;
    hdr.payload_len = 4;

    rel4u_recv_slot_t ready[16];
    size_t ready_count = 0;

    // 1. Out-of-order packet 12 arrives first -> delivered immediately
    hdr.seq_num = 12;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"ru12", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 12);
    ASSERT_EQ(ready[0].delivery_mode, REL4U_MODE_RELIABLE_UNORDERED);
    ASSERT_EQ(win.expected_seq, 10);

    // 2. Duplicate of 12 while still in window -> dropped (1)
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"ru12", ready, 16, &ready_count), 1);

    // 3. Packet 10 arrives -> delivered immediately, expected_seq advances to 11
    hdr.seq_num = 10;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"ru10", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 10);
    ASSERT_EQ(win.expected_seq, 11);

    // 4. Packet 11 arrives -> delivered immediately, expected_seq skips over already-delivered 12 to 13
    hdr.seq_num = 11;
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr, (const uint8_t*)"ru11", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 11);
    ASSERT_EQ(win.expected_seq, 13);

    rel4u_recv_window_destroy(&win);
    return true;
}

static bool test_recv_window_interleaved_modes(void) {
    rel4u_recv_window_t win;
    ASSERT_EQ(rel4u_recv_window_init(&win, 16, 1), 0);

    rel4u_recv_slot_t ready[16];
    size_t ready_count = 0;

    // Direction A: Ordered 3 buffered, Unordered 2 delivered immediately, Ordered 1 releases 1 and 3
    rel4u_header_t hdr3 = { .seq_num = 3, .delivery_mode = REL4U_MODE_RELIABLE_ORDERED, .payload_len = 2 };
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr3, (const uint8_t*)"p3", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 0);

    rel4u_header_t hdr2 = { .seq_num = 2, .delivery_mode = REL4U_MODE_RELIABLE_UNORDERED, .payload_len = 2 };
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr2, (const uint8_t*)"p2", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 2);
    ASSERT_EQ(win.expected_seq, 1);

    rel4u_header_t hdr1 = { .seq_num = 1, .delivery_mode = REL4U_MODE_RELIABLE_ORDERED, .payload_len = 2 };
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr1, (const uint8_t*)"p1", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 2);
    ASSERT_EQ(ready[0].seq_num, 1);
    ASSERT_EQ(ready[1].seq_num, 3);
    ASSERT_EQ(win.expected_seq, 4);

    // Direction B: Ordered 5 buffered, then Unordered 4 arrives at expected_seq and drains both 4 and 5
    rel4u_header_t hdr5 = { .seq_num = 5, .delivery_mode = REL4U_MODE_RELIABLE_ORDERED, .payload_len = 2 };
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr5, (const uint8_t*)"p5", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 0);

    rel4u_header_t hdr4 = { .seq_num = 4, .delivery_mode = REL4U_MODE_RELIABLE_UNORDERED, .payload_len = 2 };
    ASSERT_EQ(rel4u_recv_window_on_packet(&win, &hdr4, (const uint8_t*)"p4", ready, 16, &ready_count), 0);
    ASSERT_EQ(ready_count, 2);
    ASSERT_EQ(ready[0].seq_num, 4);
    ASSERT_EQ(ready[1].seq_num, 5);
    ASSERT_EQ(win.expected_seq, 6);

    rel4u_recv_window_destroy(&win);
    return true;
}

static bool test_send_window_expired_capping(void) {
    rel4u_send_window_t win;
    ASSERT_EQ(rel4u_send_window_init(&win, 8, 1), 0);

    for (int i = 0; i < 5; i++) {
        uint32_t s = 0;
        ASSERT_EQ(rel4u_send_window_push(&win, REL4U_MODE_RELIABLE_ORDERED, "p", 1, 1000000ULL, &s), 0);
    }

    rel4u_send_slot_t* expired[3];
    size_t exp_count = rel4u_send_window_get_expired(&win, 500000000ULL, 100, expired, 3);
    ASSERT_EQ(exp_count, 3);
    ASSERT_EQ(expired[0]->seq_num, 1);
    ASSERT_EQ(expired[1]->seq_num, 2);
    ASSERT_EQ(expired[2]->seq_num, 3);

    // Simulate retransmission of expired[0] and verify Karn's algorithm:
    // ACKing retransmitted seq 1 must NOT update RTT
    expired[0]->retransmit_count = 1;
    rel4u_rtt_t rtt;
    rel4u_rtt_init(&rtt, 200, 20, 2000);
    rel4u_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    rel4u_send_window_on_ack(&win, 1, 0, 550000000ULL, &rtt, &stats);
    ASSERT_FALSE(rtt.has_sample);

    // ACKing non-retransmitted seq 4 via SACK (ack_num=1, sack bit 2 -> seq 4) MUST update RTT & stats
    rel4u_send_window_on_ack(&win, 1, (1U << 2), 51000000ULL, &rtt, &stats);
    ASSERT_TRUE(rtt.has_sample);
    ASSERT_EQ(rtt.srtt_ms, 50);
    ASSERT_EQ(stats.current_rtt_ms, 50);
    ASSERT_EQ(stats.current_rto_ms, rtt.rto_ms);

    rel4u_send_window_destroy(&win);
    return true;
}

static bool test_window_wraparound_operations(void) {
    uint32_t init_seq = 0xFFFFFFFEU;
    rel4u_send_window_t send_win;
    rel4u_recv_window_t recv_win;
    ASSERT_EQ(rel4u_send_window_init(&send_win, 8, init_seq), 0);
    ASSERT_EQ(rel4u_recv_window_init(&recv_win, 8, init_seq), 0);

    uint32_t seqs[4];
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(rel4u_send_window_push(&send_win, REL4U_MODE_RELIABLE_ORDERED, "data", 4, 1000, &seqs[i]), 0);
    }
    ASSERT_EQ(seqs[0], 0xFFFFFFFEU);
    ASSERT_EQ(seqs[1], 0xFFFFFFFFU);
    ASSERT_EQ(seqs[2], 0x00000000U);
    ASSERT_EQ(seqs[3], 0x00000001U);

    rel4u_recv_slot_t ready[8];
    size_t ready_count = 0;
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.delivery_mode = REL4U_MODE_RELIABLE_ORDERED;
    hdr.payload_len = 4;

    // Deliver out-of-order across wraparound: seq 0 arrives while expected_seq is 0xFFFFFFFE
    hdr.seq_num = seqs[2]; // 0x00000000U
    ASSERT_EQ(rel4u_recv_window_on_packet(&recv_win, &hdr, (const uint8_t*)"data", ready, 8, &ready_count), 0);
    ASSERT_EQ(ready_count, 0);

    uint32_t ack_num = 0, sack_mask = 0;
    rel4u_recv_window_get_ack_info(&recv_win, &ack_num, &sack_mask);
    ASSERT_EQ(ack_num, 0xFFFFFFFDU);
    ASSERT_EQ(sack_mask, (1U << 2));

    // Deliver 0xFFFFFFFE -> releases 0xFFFFFFFE; 0x00000000 still waits for 0xFFFFFFFF
    hdr.seq_num = seqs[0]; // 0xFFFFFFFEU
    ASSERT_EQ(rel4u_recv_window_on_packet(&recv_win, &hdr, (const uint8_t*)"data", ready, 8, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 0xFFFFFFFEU);

    // Deliver 0xFFFFFFFF -> releases both 0xFFFFFFFF and buffered 0x00000000
    hdr.seq_num = seqs[1]; // 0xFFFFFFFFU
    ASSERT_EQ(rel4u_recv_window_on_packet(&recv_win, &hdr, (const uint8_t*)"data", ready, 8, &ready_count), 0);
    ASSERT_EQ(ready_count, 2);
    ASSERT_EQ(ready[0].seq_num, 0xFFFFFFFFU);
    ASSERT_EQ(ready[1].seq_num, 0x00000000U);

    // Deliver 0x00000001 -> releases 0x00000001
    hdr.seq_num = seqs[3]; // 0x00000001U
    ASSERT_EQ(rel4u_recv_window_on_packet(&recv_win, &hdr, (const uint8_t*)"data", ready, 8, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 0x00000001U);
    ASSERT_EQ(recv_win.expected_seq, 0x00000002U);

    rel4u_recv_window_get_ack_info(&recv_win, &ack_num, &sack_mask);
    ASSERT_EQ(ack_num, 0x00000001U);
    ASSERT_EQ(sack_mask, 0);

    rel4u_send_window_on_ack(&send_win, ack_num, sack_mask, 2000, NULL, NULL);
    ASSERT_EQ(send_win.in_flight, 0);
    ASSERT_EQ(send_win.head_seq, 0x00000002U);

    rel4u_send_window_destroy(&send_win);
    rel4u_recv_window_destroy(&recv_win);
    return true;
}

static bool test_window_invalid_init(void) {
    rel4u_send_window_t sw;
    rel4u_recv_window_t rw;
    ASSERT_EQ(rel4u_send_window_init(NULL, 16, 1), -1);
    ASSERT_EQ(rel4u_recv_window_init(NULL, 16, 1), -1);
    ASSERT_EQ(rel4u_send_window_init(&sw, 1, 1), -1);
    ASSERT_EQ(rel4u_recv_window_init(&rw, 1, 1), -1);
    ASSERT_EQ(rel4u_send_window_init(&sw, 10, 1), -1);
    ASSERT_EQ(rel4u_recv_window_init(&rw, 10, 1), -1);

    // Direct pop_ready and recv_window_reset boundary checks
    ASSERT_EQ(rel4u_recv_window_init(&rw, 8, 1), 0);
    rel4u_recv_slot_t ready[4];
    size_t ready_count = 0;
    ASSERT_EQ(rel4u_recv_window_pop_ready(NULL, ready, 4, &ready_count), -1);
    ASSERT_EQ(rel4u_recv_window_pop_ready(&rw, NULL, 4, &ready_count), -1);
    ASSERT_EQ(rel4u_recv_window_pop_ready(&rw, ready, 0, &ready_count), -1);
    ASSERT_EQ(rel4u_recv_window_pop_ready(&rw, ready, 4, NULL), -1);

    // Buffer seq 2 and 3, then arrive seq 1 with max_ready=1 so seq 2 and 3 remain for pop_ready
    rel4u_header_t hdr = { .delivery_mode = REL4U_MODE_RELIABLE_ORDERED, .payload_len = 2 };
    hdr.seq_num = 2;
    ASSERT_EQ(rel4u_recv_window_on_packet(&rw, &hdr, (const uint8_t*)"p2", ready, 1, &ready_count), 0);
    hdr.seq_num = 3;
    ASSERT_EQ(rel4u_recv_window_on_packet(&rw, &hdr, (const uint8_t*)"p3", ready, 1, &ready_count), 0);
    hdr.seq_num = 1;
    ASSERT_EQ(rel4u_recv_window_on_packet(&rw, &hdr, (const uint8_t*)"p1", ready, 1, &ready_count), 0);
    ASSERT_EQ(ready_count, 1);
    ASSERT_EQ(ready[0].seq_num, 1);

    // Pop remaining 2 ready packets via rel4u_recv_window_pop_ready
    ASSERT_EQ(rel4u_recv_window_pop_ready(&rw, ready, 4, &ready_count), 0);
    ASSERT_EQ(ready_count, 2);
    ASSERT_EQ(ready[0].seq_num, 2);
    ASSERT_EQ(ready[1].seq_num, 3);

    ASSERT_EQ(rel4u_recv_window_reset(NULL, 100), -1);
    ASSERT_EQ(rel4u_recv_window_reset(&rw, 100), 0);
    ASSERT_EQ(rw.expected_seq, 100);

    rel4u_recv_window_destroy(&rw);
    return true;
}

TEST_SUITE_BEGIN("Sliding Window & SACK Unit Tests")
    RUN_TEST(test_sequence_math);
    RUN_TEST(test_send_window_and_sack);
    RUN_TEST(test_recv_window_reassembly_and_sack);
    RUN_TEST(test_recv_window_reliable_unordered);
    RUN_TEST(test_recv_window_interleaved_modes);
    RUN_TEST(test_unreliable_ordered_mode);
    RUN_TEST(test_window_reset_and_reclaim);
    RUN_TEST(test_send_window_full_and_exhaustion);
    RUN_TEST(test_send_window_expired_capping);
    RUN_TEST(test_recv_window_duplicate_and_capacity);
    RUN_TEST(test_window_wraparound_operations);
    RUN_TEST(test_window_invalid_init);
TEST_SUITE_END()
