#include "rel4u_session.h"
#include <stdlib.h>
#include <string.h>

int rel4u_session_table_init(rel4u_session_table_t* table, uint32_t max_clients,
                              rel4u_max_clients_policy_t policy, uint32_t window_size) {
    if (!table || max_clients == 0) return -1;

    table->max_clients = max_clients;
    table->active_count = 0;
    table->full_policy = policy;

    table->sessions = (rel4u_client_session_t*)calloc(max_clients, sizeof(rel4u_client_session_t));
    if (!table->sessions) return -1;

    for (uint32_t i = 0; i < max_clients; i++) {
        table->sessions[i].client_id = i;
        table->sessions[i].state = REL4U_STATE_DISCONNECTED;
        rel4u_send_window_init(&table->sessions[i].send_win, window_size, 1);
        rel4u_recv_window_init(&table->sessions[i].recv_win, window_size, 1);
        rel4u_rtt_init(&table->sessions[i].rtt, REL4U_RTT_DEFAULT_RTO_MS, REL4U_RTT_MIN_RTO_MS, REL4U_RTT_MAX_RTO_MS);
    }

    return 0;
}

void rel4u_session_table_destroy(rel4u_session_table_t* table) {
    if (!table) return;

    if (table->sessions) {
        for (uint32_t i = 0; i < table->max_clients; i++) {
            rel4u_send_window_destroy(&table->sessions[i].send_win);
            rel4u_recv_window_destroy(&table->sessions[i].recv_win);
        }
        free(table->sessions);
        table->sessions = NULL;
    }

    table->max_clients = 0;
    table->active_count = 0;
}

rel4u_client_session_t* rel4u_session_find_by_addr(rel4u_session_table_t* table, const rel4u_net_addr_t* addr) {
    if (!table || !addr || !table->sessions) return NULL;

    for (uint32_t i = 0; i < table->max_clients; i++) {
        rel4u_client_session_t* s = &table->sessions[i];
        if (s->state != REL4U_STATE_DISCONNECTED && rel4u_net_addr_equal(&s->addr, addr)) {
            return s;
        }
    }

    return NULL;
}

rel4u_client_session_t* rel4u_session_find_by_id(rel4u_session_table_t* table, uint32_t client_id) {
    if (!table || !table->sessions || client_id >= table->max_clients) return NULL;
    rel4u_client_session_t* s = &table->sessions[client_id];
    return (s->state != REL4U_STATE_DISCONNECTED) ? s : NULL;
}

rel4u_client_session_t* rel4u_session_allocate(rel4u_session_table_t* table, const rel4u_net_addr_t* addr,
                                               uint32_t session_id, bool* out_was_evicted) {
    if (!table || !addr || !table->sessions) return NULL;
    if (out_was_evicted) *out_was_evicted = false;

    int32_t slot_idx = -1;

    if (table->active_count < table->max_clients) {
        for (uint32_t i = 0; i < table->max_clients; i++) {
            if (table->sessions[i].state == REL4U_STATE_DISCONNECTED) {
                slot_idx = (int32_t)i;
                break;
            }
        }
    } else {
        if (table->full_policy == REL4U_MAX_CLIENTS_REJECT) {
            return NULL;
        }

        /* LRU eviction: find active session with lowest last_activity_ns */
        uint64_t oldest_time = UINT64_MAX;
        for (uint32_t i = 0; i < table->max_clients; i++) {
            if (table->sessions[i].state != REL4U_STATE_DISCONNECTED) {
                if (table->sessions[i].last_activity_ns < oldest_time) {
                    oldest_time = table->sessions[i].last_activity_ns;
                    slot_idx = (int32_t)i;
                }
            }
        }

        if (slot_idx != -1) {
            if (out_was_evicted) *out_was_evicted = true;
            table->active_count--;
        }
    }

    if (slot_idx == -1) return NULL;

    rel4u_client_session_t* s = &table->sessions[slot_idx];
    s->session_id = session_id;
    s->addr = *addr;
    s->state = REL4U_STATE_CONNECTING;
    s->ack_pending = false;
    memset(&s->stats, 0, sizeof(s->stats));

    uint32_t win_cap = s->send_win.capacity ? s->send_win.capacity : 64;
    rel4u_send_window_destroy(&s->send_win);
    rel4u_recv_window_destroy(&s->recv_win);
    rel4u_send_window_init(&s->send_win, win_cap, 1);
    rel4u_recv_window_init(&s->recv_win, win_cap, 1);
    rel4u_rtt_init(&s->rtt, REL4U_RTT_DEFAULT_RTO_MS, REL4U_RTT_MIN_RTO_MS, REL4U_RTT_MAX_RTO_MS);

    table->active_count++;
    return s;
}

void rel4u_session_touch_lru(rel4u_session_table_t* table, rel4u_client_session_t* session, uint64_t now_ns) {
    (void)table;
    if (session) {
        session->last_activity_ns = now_ns;
    }
}

void rel4u_session_free(rel4u_session_table_t* table, rel4u_client_session_t* session) {
    if (!table || !session || session->state == REL4U_STATE_DISCONNECTED) return;
    session->state = REL4U_STATE_DISCONNECTED;
    session->session_id = 0;
    if (table->active_count > 0) table->active_count--;
}
