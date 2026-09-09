#ifndef REL4U_SESSION_H
#define REL4U_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../platform/rel4u_net.h"
#include "rel4u_window.h"
#include "rel4u_rtt.h"
#include "../../include/rel4u.h"

typedef struct rel4u_client_session {
    uint32_t           client_id;        /* Index in session array */
    uint32_t           session_id;       /* Random 32-bit token */
    rel4u_net_addr_t   addr;
    rel4u_conn_state_t state;
    rel4u_send_window_t send_win;
    rel4u_recv_window_t recv_win;
    rel4u_rtt_t        rtt;
    rel4u_stats_t      stats;

    uint64_t           last_activity_ns;
    uint64_t           last_heartbeat_sent_ns;
    uint64_t           last_ack_sent_ns;
    bool               ack_pending;
} rel4u_client_session_t;

typedef struct rel4u_session_table {
    rel4u_client_session_t*    sessions;
    uint32_t                   max_clients;
    uint32_t                   active_count;
    rel4u_max_clients_policy_t full_policy;
} rel4u_session_table_t;

int  rel4u_session_table_init(rel4u_session_table_t* table, uint32_t max_clients,
                              rel4u_max_clients_policy_t policy, uint32_t window_size);
void rel4u_session_table_destroy(rel4u_session_table_t* table);

rel4u_client_session_t* rel4u_session_find_by_addr(rel4u_session_table_t* table, const rel4u_net_addr_t* addr);
rel4u_client_session_t* rel4u_session_find_by_id(rel4u_session_table_t* table, uint32_t client_id);

rel4u_client_session_t* rel4u_session_allocate(rel4u_session_table_t* table, const rel4u_net_addr_t* addr,
                                               uint32_t session_id, bool* out_was_evicted);

void rel4u_session_touch_lru(rel4u_session_table_t* table, rel4u_client_session_t* session, uint64_t now_ns);
void rel4u_session_free(rel4u_session_table_t* table, rel4u_client_session_t* session);

#endif /* REL4U_SESSION_H */
