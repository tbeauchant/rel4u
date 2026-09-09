#include "rel4u_window.h"
#include "rel4u_mpmc.h"
#include <stdlib.h>
#include <string.h>

int rel4u_send_window_init(rel4u_send_window_t* win, uint32_t capacity, uint32_t initial_seq) {
    if (!win || capacity < 2 || (capacity & (capacity - 1)) != 0) {
        return -1;
    }

    win->slots = (rel4u_send_slot_t*)calloc(capacity, sizeof(rel4u_send_slot_t));
    if (!win->slots) {
        return -1;
    }

    win->capacity = capacity;
    win->head_seq = initial_seq;
    win->next_seq = initial_seq;
    win->next_unrel_seq = initial_seq;
    win->in_flight = 0;
    return 0;
}

void rel4u_send_window_destroy(rel4u_send_window_t* win) {
    if (!win) return;
    if (win->slots) {
        free(win->slots);
        win->slots = NULL;
    }
    win->capacity = 0;
    win->in_flight = 0;
}

bool rel4u_send_window_can_send(const rel4u_send_window_t* win) {
    if (!win) return false;
    return (win->in_flight < win->capacity) &&
           (rel4u_seq_diff(win->next_seq, win->head_seq) < (int32_t)win->capacity);
}

int rel4u_send_window_push(rel4u_send_window_t* win, rel4u_delivery_mode_t mode,
                            const void* data, size_t len, uint64_t now_ns, uint32_t* out_seq) {
    if (!win || !data || len > REL4U_DEFAULT_MTU) {
        return -1;
    }

    if (mode == REL4U_MODE_RELIABLE_ORDERED || mode == REL4U_MODE_RELIABLE_UNORDERED) {
        if (win->in_flight >= win->capacity ||
            rel4u_seq_diff(win->next_seq, win->head_seq) >= (int32_t)win->capacity) {
            return -2; /* Window full */
        }

        uint32_t seq = win->next_seq++;
        if (out_seq) *out_seq = seq;

        uint32_t slot_idx = seq & (win->capacity - 1);
        rel4u_send_slot_t* slot = &win->slots[slot_idx];

        slot->seq_num = seq;
        slot->first_sent_time_ns = now_ns;
        slot->last_sent_time_ns = now_ns;
        slot->payload_len = (uint16_t)len;
        slot->retransmit_count = 0;
        slot->delivery_mode = (uint8_t)mode;
        slot->in_use = true;
        slot->acked = false;
        memcpy(slot->payload, data, len);

        win->in_flight++;
    } else {
        uint32_t seq = win->next_unrel_seq++;
        if (out_seq) *out_seq = seq;
    }

    return 0;
}

void rel4u_send_window_on_ack(rel4u_send_window_t* win, uint32_t ack_num, uint32_t sack_mask,
                              uint64_t now_ns, rel4u_rtt_t* rtt, rel4u_stats_t* stats) {
    if (!win) return;

    /* 1. Process cumulative ACK */
    while (rel4u_seq_lte(win->head_seq, ack_num) && win->head_seq != win->next_seq) {
        uint32_t slot_idx = win->head_seq & (win->capacity - 1);
        rel4u_send_slot_t* slot = &win->slots[slot_idx];

        if (slot->in_use && slot->seq_num == win->head_seq) {
            if (!slot->acked) {
                slot->acked = true;
                if (win->in_flight > 0) win->in_flight--;

                if (slot->retransmit_count == 0 && rtt && now_ns >= slot->first_sent_time_ns) {
                    uint64_t rtt_sample_ns = now_ns - slot->first_sent_time_ns;
                    uint32_t rtt_sample_ms = (uint32_t)(rtt_sample_ns / 1000000ULL);
                    rel4u_rtt_update_sample(rtt, rtt_sample_ms);
                    if (stats) {
                        stats->current_rtt_ms = rtt->srtt_ms;
                        stats->current_rto_ms = rtt->rto_ms;
                    }
                }
            }
            slot->in_use = false;
        }
        win->head_seq++;
    }

    /* 2. Process Selective ACKs (SACK) */
    if (sack_mask != 0) {
        for (int i = 0; i < 32; i++) {
            if (sack_mask & (1U << i)) {
                uint32_t sack_seq = ack_num + 1 + (uint32_t)i;
                if (rel4u_seq_lt(sack_seq, win->next_seq) && rel4u_seq_gte(sack_seq, win->head_seq)) {
                    uint32_t slot_idx = sack_seq & (win->capacity - 1);
                    rel4u_send_slot_t* slot = &win->slots[slot_idx];

                    if (slot->in_use && slot->seq_num == sack_seq && !slot->acked) {
                        slot->acked = true;
                        if (win->in_flight > 0) win->in_flight--;

                        if (slot->retransmit_count == 0 && rtt && now_ns >= slot->first_sent_time_ns) {
                            uint64_t rtt_sample_ns = now_ns - slot->first_sent_time_ns;
                            uint32_t rtt_sample_ms = (uint32_t)(rtt_sample_ns / 1000000ULL);
                            rel4u_rtt_update_sample(rtt, rtt_sample_ms);
                            if (stats) {
                                stats->current_rtt_ms = rtt->srtt_ms;
                                stats->current_rto_ms = rtt->rto_ms;
                            }
                        }
                    }
                }
            }
        }
    }
}

size_t rel4u_send_window_get_expired(rel4u_send_window_t* win, uint64_t now_ns, uint32_t rto_ms,
                                     rel4u_send_slot_t** out_slots, size_t max_slots) {
    if (!win || !out_slots || max_slots == 0) return 0;

    size_t count = 0;
    uint64_t rto_ns = (uint64_t)rto_ms * 1000000ULL;

    for (uint32_t seq = win->head_seq; rel4u_seq_lt(seq, win->next_seq); seq++) {
        uint32_t slot_idx = seq & (win->capacity - 1);
        rel4u_send_slot_t* slot = &win->slots[slot_idx];

        if (slot->in_use && slot->seq_num == seq && !slot->acked) {
            if (now_ns >= slot->last_sent_time_ns && (now_ns - slot->last_sent_time_ns) >= rto_ns) {
                out_slots[count++] = slot;
                if (count >= max_slots) break;
            }
        }
    }

    return count;
}

int rel4u_send_window_reset(rel4u_send_window_t* win, uint32_t initial_seq) {
    if (!win || !win->slots || win->capacity == 0) return -1;
    memset(win->slots, 0, sizeof(rel4u_send_slot_t) * win->capacity);
    win->head_seq = initial_seq;
    win->next_seq = initial_seq;
    win->next_unrel_seq = initial_seq;
    win->in_flight = 0;
    return 0;
}

size_t rel4u_send_window_reclaim_unacked(rel4u_send_window_t* win, rel4u_mpmc_queue_t* queue) {
    if (!win || !queue || !win->slots) return 0;
    size_t reclaimed = 0;

    if (rel4u_seq_gt(win->next_seq, win->head_seq)) {
        for (uint32_t seq = win->next_seq; rel4u_seq_gt(seq, win->head_seq); seq--) {
            uint32_t current_seq = seq - 1;
            uint32_t idx = current_seq & (win->capacity - 1);
            rel4u_send_slot_t* slot = &win->slots[idx];
            if (slot->in_use && slot->seq_num == current_seq && !slot->acked &&
                (slot->delivery_mode == REL4U_MODE_RELIABLE_ORDERED ||
                 slot->delivery_mode == REL4U_MODE_RELIABLE_UNORDERED)) {
                rel4u_mpmc_item_t item;
                memset(&item, 0, sizeof(item));
                item.client_id = 0;
                item.mode = slot->delivery_mode;
                item.len = slot->payload_len;
                memcpy(item.data, slot->payload, slot->payload_len);
                if (rel4u_mpmc_push_front(queue, &item)) {
                    reclaimed++;
                }
            }
        }
    }
    return reclaimed;
}

int rel4u_recv_window_init(rel4u_recv_window_t* win, uint32_t capacity, uint32_t initial_seq) {
    if (!win || capacity < 2 || (capacity & (capacity - 1)) != 0) {
        return -1;
    }

    win->slots = (rel4u_recv_slot_t*)calloc(capacity, sizeof(rel4u_recv_slot_t));
    if (!win->slots) {
        return -1;
    }

    win->capacity = capacity;
    win->expected_seq = initial_seq;
    win->highest_unrel_seq = 0;
    win->has_highest_unrel = false;
    return 0;
}

void rel4u_recv_window_destroy(rel4u_recv_window_t* win) {
    if (!win) return;
    if (win->slots) {
        free(win->slots);
        win->slots = NULL;
    }
    win->capacity = 0;
}

int rel4u_recv_window_reset(rel4u_recv_window_t* win, uint32_t initial_seq) {
    if (!win || !win->slots || win->capacity == 0) return -1;
    memset(win->slots, 0, sizeof(rel4u_recv_slot_t) * win->capacity);
    win->expected_seq = initial_seq;
    win->highest_unrel_seq = 0;
    win->has_highest_unrel = false;
    return 0;
}

int rel4u_recv_window_on_packet(rel4u_recv_window_t* win, const rel4u_header_t* hdr, const uint8_t* payload,
                                rel4u_recv_slot_t* out_ready_slots, size_t max_ready, size_t* out_ready_count) {
    if (!win || !hdr || !out_ready_slots || !out_ready_count || max_ready == 0) {
        return -1;
    }

    *out_ready_count = 0;
    rel4u_delivery_mode_t mode = (rel4u_delivery_mode_t)hdr->delivery_mode;

    /* Mode 0: Unreliable Unordered */
    if (mode == REL4U_MODE_UNRELIABLE_UNORDERED) {
        out_ready_slots[0].seq_num = hdr->seq_num;
        out_ready_slots[0].delivery_mode = (uint8_t)mode;
        out_ready_slots[0].payload_len = hdr->payload_len;
        out_ready_slots[0].received = true;
        if (hdr->payload_len > 0 && payload) {
            memcpy(out_ready_slots[0].payload, payload, hdr->payload_len);
        }
        *out_ready_count = 1;
        return 0;
    }

    /* Mode 1: Unreliable Ordered */
    if (mode == REL4U_MODE_UNRELIABLE_ORDERED) {
        if (!win->has_highest_unrel || rel4u_seq_gt(hdr->seq_num, win->highest_unrel_seq)) {
            win->highest_unrel_seq = hdr->seq_num;
            win->has_highest_unrel = true;
            out_ready_slots[0].seq_num = hdr->seq_num;
            out_ready_slots[0].delivery_mode = (uint8_t)mode;
            out_ready_slots[0].payload_len = hdr->payload_len;
            out_ready_slots[0].received = true;
            if (hdr->payload_len > 0 && payload) {
                memcpy(out_ready_slots[0].payload, payload, hdr->payload_len);
            }
            *out_ready_count = 1;
            return 0;
        }
        return 1; /* Stale packet, drop */
    }

    /* Mode 2: Reliable Unordered */
    if (mode == REL4U_MODE_RELIABLE_UNORDERED) {
        if (rel4u_seq_lt(hdr->seq_num, win->expected_seq)) {
            return 1; /* Old/duplicate packet, drop */
        }
        int32_t diff = rel4u_seq_diff(hdr->seq_num, win->expected_seq);
        if (diff >= (int32_t)win->capacity) {
            return -2; /* Beyond window capacity */
        }
        uint32_t slot_idx = hdr->seq_num & (win->capacity - 1);
        rel4u_recv_slot_t* slot = &win->slots[slot_idx];
        if (slot->received && slot->seq_num == hdr->seq_num) {
            return 1; /* Duplicate in window, drop */
        }
        slot->received = true;
        slot->seq_num = hdr->seq_num;
        slot->payload_len = hdr->payload_len;
        slot->delivery_mode = (uint8_t)mode;

        out_ready_slots[0].seq_num = hdr->seq_num;
        out_ready_slots[0].delivery_mode = (uint8_t)mode;
        out_ready_slots[0].payload_len = hdr->payload_len;
        out_ready_slots[0].received = true;
        if (hdr->payload_len > 0 && payload) {
            memcpy(out_ready_slots[0].payload, payload, hdr->payload_len);
        }
        *out_ready_count = 1;

        /* Advance expected_seq over contiguous already delivered/received slots */
        while (1) {
            uint32_t exp_idx = win->expected_seq & (win->capacity - 1);
            rel4u_recv_slot_t* exp_slot = &win->slots[exp_idx];
            if (exp_slot->received && exp_slot->seq_num == win->expected_seq) {
                if (exp_slot->delivery_mode == REL4U_MODE_RELIABLE_UNORDERED) {
                    exp_slot->received = false;
                    win->expected_seq++;
                } else if (exp_slot->delivery_mode == REL4U_MODE_RELIABLE_ORDERED) {
                    if (*out_ready_count < max_ready) {
                        out_ready_slots[*out_ready_count] = *exp_slot;
                        (*out_ready_count)++;
                        exp_slot->received = false;
                        win->expected_seq++;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        return 0;
    }

    /* Mode 3: Reliable Ordered */
    if (mode == REL4U_MODE_RELIABLE_ORDERED) {
        if (rel4u_seq_lt(hdr->seq_num, win->expected_seq)) {
            return 1; /* Duplicate packet already delivered, drop */
        }

        int32_t diff = rel4u_seq_diff(hdr->seq_num, win->expected_seq);
        if (diff >= (int32_t)win->capacity) {
            return -2; /* Beyond window capacity */
        }

        uint32_t slot_idx = hdr->seq_num & (win->capacity - 1);
        rel4u_recv_slot_t* slot = &win->slots[slot_idx];

        if (slot->received && slot->seq_num == hdr->seq_num) {
            return 1; /* Duplicate in reassembly buffer, drop */
        }

        slot->seq_num = hdr->seq_num;
        slot->payload_len = hdr->payload_len;
        slot->delivery_mode = (uint8_t)mode;
        slot->received = true;
        if (hdr->payload_len > 0 && payload) {
            memcpy(slot->payload, payload, hdr->payload_len);
        }

        /* Drain contiguous in-order packets */
        size_t ready_count = 0;
        while (ready_count < max_ready) {
            uint32_t exp_idx = win->expected_seq & (win->capacity - 1);
            rel4u_recv_slot_t* exp_slot = &win->slots[exp_idx];

            if (exp_slot->received && exp_slot->seq_num == win->expected_seq) {
                if (exp_slot->delivery_mode == REL4U_MODE_RELIABLE_ORDERED) {
                    out_ready_slots[ready_count] = *exp_slot;
                    ready_count++;
                }
                exp_slot->received = false; /* Clear slot */
                win->expected_seq++;
            } else {
                break;
            }
        }
        *out_ready_count = ready_count;
        return 0;
    }

    return -1;
}

void rel4u_recv_window_get_ack_info(const rel4u_recv_window_t* win, uint32_t* out_ack_num, uint32_t* out_sack_mask) {
    if (!win) return;

    if (out_ack_num) {
        *out_ack_num = win->expected_seq - 1;
    }

    if (out_sack_mask) {
        uint32_t mask = 0;
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t target_seq = win->expected_seq + i;
            uint32_t slot_idx = target_seq & (win->capacity - 1);
            const rel4u_recv_slot_t* slot = &win->slots[slot_idx];

            if (slot->received && slot->seq_num == target_seq) {
                mask |= (1U << i);
            }
        }
        *out_sack_mask = mask;
    }
}
