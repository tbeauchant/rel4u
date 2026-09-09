#ifndef REL4U_WINDOW_H
#define REL4U_WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rel4u_rtt.h"
#include "rel4u_packet.h"
#include "../../include/rel4u.h"

struct rel4u_mpmc_queue;

/* Sequence Arithmetic */
static inline bool rel4u_seq_lt(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b)) < 0;
}

static inline bool rel4u_seq_lte(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b)) <= 0;
}

static inline bool rel4u_seq_gt(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b)) > 0;
}

static inline bool rel4u_seq_gte(uint32_t a, uint32_t b) {
    return ((int32_t)(a - b)) >= 0;
}

static inline int32_t rel4u_seq_diff(uint32_t a, uint32_t b) {
    return (int32_t)(a - b);
}

/* Send Window Slot */
typedef struct rel4u_send_slot {
    uint32_t seq_num;
    uint64_t first_sent_time_ns;
    uint64_t last_sent_time_ns;
    uint16_t payload_len;
    uint8_t  retransmit_count;
    uint8_t  delivery_mode;
    bool     in_use;
    bool     acked;
    uint8_t  payload[REL4U_DEFAULT_MTU];
} rel4u_send_slot_t;

/* Send Window */
typedef struct rel4u_send_window {
    rel4u_send_slot_t* slots;
    uint32_t           capacity;          /* Must be power of 2 */
    uint32_t           head_seq;          /* Oldest unacked sequence */
    uint32_t           next_seq;          /* Next reliable sequence number to assign */
    uint32_t           next_unrel_seq;    /* Next unreliable sequence number to assign */
    uint32_t           in_flight;         /* Number of unacked reliable packets */
} rel4u_send_window_t;

/* Receive Window Slot */
typedef struct rel4u_recv_slot {
    uint32_t seq_num;
    uint16_t payload_len;
    uint8_t  delivery_mode;
    bool     received;
    uint8_t  payload[REL4U_DEFAULT_MTU];
} rel4u_recv_slot_t;

/* Receive Window */
typedef struct rel4u_recv_window {
    rel4u_recv_slot_t* slots;
    uint32_t           capacity;       /* Must be power of 2 */
    uint32_t           expected_seq;   /* Next in-order sequence expected */
    uint32_t           highest_unrel_seq; /* For REL4U_MODE_UNRELIABLE_ORDERED */
    bool               has_highest_unrel;
} rel4u_recv_window_t;

/* Send Window Functions */
int  rel4u_send_window_init(rel4u_send_window_t* win, uint32_t capacity, uint32_t initial_seq);
void rel4u_send_window_destroy(rel4u_send_window_t* win);
bool rel4u_send_window_can_send(const rel4u_send_window_t* win);
int  rel4u_send_window_push(rel4u_send_window_t* win, rel4u_delivery_mode_t mode,
                            const void* data, size_t len, uint64_t now_ns, uint32_t* out_seq);
void rel4u_send_window_on_ack(rel4u_send_window_t* win, uint32_t ack_num, uint32_t sack_mask,
                              uint64_t now_ns, rel4u_rtt_t* rtt, rel4u_stats_t* stats);
size_t rel4u_send_window_get_expired(rel4u_send_window_t* win, uint64_t now_ns, uint32_t rto_ms,
                                     rel4u_send_slot_t** out_slots, size_t max_slots);
int    rel4u_send_window_reset(rel4u_send_window_t* win, uint32_t initial_seq);
size_t rel4u_send_window_reclaim_unacked(rel4u_send_window_t* win, struct rel4u_mpmc_queue* queue);

/* Receive Window Functions */
int  rel4u_recv_window_init(rel4u_recv_window_t* win, uint32_t capacity, uint32_t initial_seq);
void rel4u_recv_window_destroy(rel4u_recv_window_t* win);
int  rel4u_recv_window_reset(rel4u_recv_window_t* win, uint32_t initial_seq);

/**
 * @brief Process an incoming packet into receive window.
 * @param win Receive window pointer.
 * @param hdr Packet header.
 * @param payload Raw payload buffer.
 * @param out_ready_slots Output array for contiguous in-order ready packets to be popped.
 * @param max_ready Max capacity of out_ready_slots.
 * @param out_ready_count Output number of ready packets populated.
 * @return 0 if accepted, 1 if duplicate/stale (drop), negative on error.
 */
int  rel4u_recv_window_on_packet(rel4u_recv_window_t* win, const rel4u_header_t* hdr, const uint8_t* payload,
                                 rel4u_recv_slot_t* out_ready_slots, size_t max_ready, size_t* out_ready_count);

/**
 * @brief Calculate current ack_num and 32-bit sack_mask from receive window state.
 */
void rel4u_recv_window_get_ack_info(const rel4u_recv_window_t* win, uint32_t* out_ack_num, uint32_t* out_sack_mask);

#endif /* REL4U_WINDOW_H */
