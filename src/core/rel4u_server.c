#include "../../include/rel4u.h"
#include "../platform/rel4u_net.h"
#include "../platform/rel4u_threads.h"
#include "../platform/rel4u_time.h"
#include "rel4u_mpmc.h"
#include "rel4u_packet.h"
#include "rel4u_window.h"
#include "rel4u_rtt.h"
#include "rel4u_session.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

struct rel4u_server {
    rel4u_server_config_t  config;
    rel4u_socket_t         sock;
    rel4u_net_addr_t       bind_addr;
    rel4u_wakeup_pipe_t    wakeup_pipe;
    rel4u_thread_t         worker_thread;
    atomic_bool            running;

    rel4u_mpmc_queue_t     send_queue;
    rel4u_mpmc_queue_t     recv_queue;
    rel4u_session_table_t  session_table;

    rel4u_mutex_t          table_mutex;
    rel4u_mutex_t          stats_mutex;
    rel4u_stats_t          stats;
};

static void* server_worker_thread_func(void* arg);

rel4u_server_t* rel4u_server_create(const rel4u_server_config_t* config) {
    if (!config || config->bind_port == 0) {
        return NULL;
    }

    rel4u_server_t* s = (rel4u_server_t*)calloc(1, sizeof(rel4u_server_t));
    if (!s) return NULL;

    s->config = *config;
    if (s->config.max_clients == 0) s->config.max_clients = 64;
    if (s->config.mtu == 0) s->config.mtu = REL4U_DEFAULT_MTU;
    if (s->config.send_queue_capacity < 8) s->config.send_queue_capacity = 256;
    if (s->config.recv_queue_capacity < 8) s->config.recv_queue_capacity = 256;
    if (s->config.window_size < 8) s->config.window_size = 64;
    if (s->config.heartbeat_interval_ms == 0) s->config.heartbeat_interval_ms = 1000;
    if (s->config.inactivity_timeout_ms == 0) s->config.inactivity_timeout_ms = 5000;

    s->sock = REL4U_INVALID_SOCKET;
    atomic_store(&s->running, false);

    if (s->config.memory_mode == REL4U_MEM_DYNAMIC) {
        if (s->config.max_send_memory_bytes == 0) s->config.max_send_memory_bytes = 1024 * 1024;
        if (s->config.max_recv_memory_bytes == 0) s->config.max_recv_memory_bytes = 1024 * 1024;
    }

    if (rel4u_mpmc_init(&s->send_queue, s->config.memory_mode, s->config.queue_full_policy,
                        s->config.send_queue_capacity, s->config.max_send_memory_bytes) != 0) {
        free(s);
        return NULL;
    }

    if (rel4u_mpmc_init(&s->recv_queue, s->config.memory_mode, s->config.queue_full_policy,
                        s->config.recv_queue_capacity, s->config.max_recv_memory_bytes) != 0) {
        rel4u_mpmc_destroy(&s->send_queue);
        free(s);
        return NULL;
    }

    if (rel4u_session_table_init(&s->session_table, s->config.max_clients,
                                 s->config.full_policy, s->config.window_size) != 0) {
        rel4u_mpmc_destroy(&s->recv_queue);
        rel4u_mpmc_destroy(&s->send_queue);
        free(s);
        return NULL;
    }

    rel4u_mutex_init(&s->table_mutex);
    rel4u_mutex_init(&s->stats_mutex);

    return s;
}

int rel4u_server_start(rel4u_server_t* s) {
    if (!s) return REL4U_ERR_INVALID_PARAM;
    if (atomic_load(&s->running)) return REL4U_OK;

    rel4u_net_init();

    const char* bind_ip = s->config.bind_address ? s->config.bind_address : "0.0.0.0";
    if (rel4u_net_addr_from_string(&s->bind_addr, bind_ip, s->config.bind_port) != 0) {
        return REL4U_ERR_INVALID_PARAM;
    }

    s->sock = rel4u_net_socket_create_udp(s->bind_addr.addr.ss_family == AF_INET6);
    if (s->sock == REL4U_INVALID_SOCKET) {
        return REL4U_ERR_SOCKET;
    }

    rel4u_net_socket_set_reuseaddr(s->sock, true);

    if (rel4u_net_socket_bind(s->sock, &s->bind_addr) != 0) {
        rel4u_net_socket_close(s->sock);
        s->sock = REL4U_INVALID_SOCKET;
        return REL4U_ERR_SOCKET;
    }

    if (rel4u_wakeup_pipe_create(&s->wakeup_pipe) != 0) {
        rel4u_net_socket_close(s->sock);
        s->sock = REL4U_INVALID_SOCKET;
        return REL4U_ERR_SYSTEM;
    }

    atomic_store(&s->running, true);

    if (rel4u_thread_create(&s->worker_thread, server_worker_thread_func, s) != 0) {
        atomic_store(&s->running, false);
        rel4u_wakeup_pipe_close(&s->wakeup_pipe);
        rel4u_net_socket_close(s->sock);
        s->sock = REL4U_INVALID_SOCKET;
        return REL4U_ERR_SYSTEM;
    }

    return REL4U_OK;
}

int rel4u_server_stop(rel4u_server_t* s) {
    if (!s) return REL4U_ERR_INVALID_PARAM;
    if (!atomic_load(&s->running)) return REL4U_OK;

    atomic_store(&s->running, false);
    rel4u_wakeup_pipe_signal(&s->wakeup_pipe);
    rel4u_mpmc_wake_all(&s->recv_queue);
    rel4u_thread_join(s->worker_thread);

    rel4u_wakeup_pipe_close(&s->wakeup_pipe);
    rel4u_net_socket_close(s->sock);
    s->sock = REL4U_INVALID_SOCKET;

    return REL4U_OK;
}

void rel4u_server_destroy(rel4u_server_t* s) {
    if (!s) return;
    rel4u_server_stop(s);

    rel4u_mpmc_destroy(&s->send_queue);
    rel4u_mpmc_destroy(&s->recv_queue);
    rel4u_session_table_destroy(&s->session_table);
    rel4u_mutex_destroy(&s->table_mutex);
    rel4u_mutex_destroy(&s->stats_mutex);

    free(s);
}

int rel4u_server_send(rel4u_server_t* s, uint32_t client_id,
                      rel4u_delivery_mode_t mode,
                      const void* data, size_t len) {
    if (!s || !data) return REL4U_ERR_INVALID_PARAM;
    if (len > REL4U_MAX_PAYLOAD(s->config.mtu)) return REL4U_ERR_MSG_TOO_LARGE;
    if (!atomic_load(&s->running)) return REL4U_ERR_NOT_CONNECTED;

    rel4u_mpmc_item_t item;
    item.client_id = client_id;
    item.mode = (uint8_t)mode;
    item.len = (uint16_t)len;
    memcpy(item.data, data, len);

    if (!rel4u_mpmc_try_push(&s->send_queue, &item)) {
        rel4u_mutex_lock(&s->stats_mutex);
        s->stats.packets_dropped_queue_full = rel4u_mpmc_dropped_count(&s->send_queue) +
                                              rel4u_mpmc_dropped_count(&s->recv_queue);
        rel4u_mutex_unlock(&s->stats_mutex);
        return REL4U_ERR_QUEUE_FULL;
    }

    rel4u_wakeup_pipe_signal(&s->wakeup_pipe);
    return REL4U_OK;
}

int rel4u_server_recv(rel4u_server_t* s, uint32_t* out_client_id,
                      void* buffer, size_t buffer_size,
                      size_t* out_len, int32_t timeout_ms) {
    if (!s || !buffer || buffer_size == 0) return REL4U_ERR_INVALID_PARAM;

    rel4u_mpmc_item_t item;
    if (!rel4u_mpmc_pop_wait(&s->recv_queue, &item, timeout_ms)) {
        return (timeout_ms == 0) ? REL4U_ERR_QUEUE_EMPTY : REL4U_ERR_TIMEOUT;
    }

    if (out_client_id) *out_client_id = item.client_id;
    size_t copy_len = (item.len <= buffer_size) ? item.len : buffer_size;
    memcpy(buffer, item.data, copy_len);
    if (out_len) *out_len = copy_len;

    return REL4U_OK;
}

int rel4u_server_disconnect_client(rel4u_server_t* s, uint32_t client_id) {
    if (!s) return REL4U_ERR_INVALID_PARAM;

    rel4u_mutex_lock(&s->table_mutex);
    rel4u_client_session_t* session = rel4u_session_find_by_id(&s->session_table, client_id);
    if (!session) {
        rel4u_mutex_unlock(&s->table_mutex);
        return REL4U_ERR_CLIENT_NOT_FOUND;
    }

    /* Send DISCONNECT packet */
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = REL4U_PKT_DISCONNECT;
    hdr.session_id = session->session_id;

    uint8_t buf[REL4U_HEADER_LEN];
    rel4u_packet_encode_header(&hdr, buf, sizeof(buf));
    rel4u_net_sendto(s->sock, buf, sizeof(buf), &session->addr);

    rel4u_session_free(&s->session_table, session);
    rel4u_mutex_unlock(&s->table_mutex);

    return REL4U_OK;
}

int rel4u_server_get_stats(rel4u_server_t* s, rel4u_stats_t* out_stats) {
    if (!s || !out_stats) return REL4U_ERR_INVALID_PARAM;
    rel4u_mutex_lock(&s->stats_mutex);
    *out_stats = s->stats;
    out_stats->packets_dropped_queue_full = rel4u_mpmc_dropped_count(&s->send_queue) +
                                          rel4u_mpmc_dropped_count(&s->recv_queue);
    out_stats->active_connections = s->session_table.active_count;
    out_stats->send_queue_occupancy = (uint32_t)rel4u_mpmc_size(&s->send_queue);
    out_stats->recv_queue_occupancy = (uint32_t)rel4u_mpmc_size(&s->recv_queue);
    out_stats->send_queue_bytes = rel4u_mpmc_bytes(&s->send_queue);
    out_stats->recv_queue_bytes = rel4u_mpmc_bytes(&s->recv_queue);
    rel4u_mutex_unlock(&s->stats_mutex);
    return REL4U_OK;
}

int rel4u_server_get_client_stats(rel4u_server_t* s, uint32_t client_id, rel4u_stats_t* out_stats) {
    if (!s || !out_stats) return REL4U_ERR_INVALID_PARAM;

    rel4u_mutex_lock(&s->table_mutex);
    rel4u_client_session_t* session = rel4u_session_find_by_id(&s->session_table, client_id);
    if (!session) {
        rel4u_mutex_unlock(&s->table_mutex);
        return REL4U_ERR_CLIENT_NOT_FOUND;
    }
    *out_stats = session->stats;
    out_stats->current_rto_ms = rel4u_rtt_get_rto(&session->rtt);
    rel4u_mutex_unlock(&s->table_mutex);

    return REL4U_OK;
}

/* Helper to send packet to client */
static void server_send_packet(rel4u_server_t* s, rel4u_client_session_t* session,
                               rel4u_pkt_type_t type, rel4u_delivery_mode_t mode,
                               uint32_t seq, const void* payload, size_t payload_len) {
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = (uint8_t)type;
    hdr.delivery_mode = (uint8_t)mode;
    hdr.payload_len = (uint16_t)payload_len;
    hdr.session_id = session->session_id;
    hdr.seq_num = seq;

    rel4u_recv_window_get_ack_info(&session->recv_win, &hdr.ack_num, &hdr.sack_mask);

    uint8_t raw_buf[REL4U_DEFAULT_MTU];
    int hdr_len = rel4u_packet_encode_header(&hdr, raw_buf, sizeof(raw_buf));
    if (hdr_len <= 0) return;

    if (payload_len > 0 && payload) {
        memcpy(raw_buf + hdr_len, payload, payload_len);
    }

    size_t total_len = (size_t)hdr_len + payload_len;
    int sent = rel4u_net_sendto(s->sock, raw_buf, total_len, &session->addr);
    if (sent > 0) {
        session->stats.packets_sent++;
        session->stats.bytes_sent += (uint64_t)sent;

        rel4u_mutex_lock(&s->stats_mutex);
        s->stats.packets_sent++;
        s->stats.bytes_sent += (uint64_t)sent;
        rel4u_mutex_unlock(&s->stats_mutex);

        session->last_ack_sent_ns = rel4u_time_now_ns();
        session->ack_pending = false;
    }
}

static void* server_worker_thread_func(void* arg) {
    rel4u_server_t* s = (rel4u_server_t*)arg;
    uint8_t in_buf[REL4U_DEFAULT_MTU];
    rel4u_recv_slot_t ready_slots[64];

    while (atomic_load(&s->running)) {
        uint64_t now_ns = rel4u_time_now_ns();

        /* 1. Poll socket and wakeup pipe */
        bool sock_read = false, pipe_read = false;
        rel4u_net_poll(s->sock, &s->wakeup_pipe, 10, &sock_read, &pipe_read);

        if (pipe_read) {
            rel4u_wakeup_pipe_drain(&s->wakeup_pipe);
        }

        /* 2. Process incoming socket datagrams */
        if (sock_read) {
            rel4u_net_addr_t src_addr;
            int bytes_read;
            while ((bytes_read = rel4u_net_recvfrom(s->sock, in_buf, sizeof(in_buf), &src_addr)) > 0) {
                rel4u_header_t hdr;
                if (rel4u_packet_decode_header(in_buf, (size_t)bytes_read, &hdr) != 0) {
                    continue; /* Bad packet */
                }

                now_ns = rel4u_time_now_ns();

                rel4u_mutex_lock(&s->stats_mutex);
                s->stats.packets_recv++;
                s->stats.bytes_recv += (uint64_t)bytes_read;
                rel4u_mutex_unlock(&s->stats_mutex);

                rel4u_mutex_lock(&s->table_mutex);

                /* Handle Connection Request (SYN) */
                if (hdr.packet_type == REL4U_PKT_CONNECT_REQ) {
                    rel4u_client_session_t* session = rel4u_session_find_by_addr(&s->session_table, &src_addr);
                    if (!session) {
                        bool was_evicted = false;
                        uint32_t new_session_id = (uint32_t)(now_ns ^ (uintptr_t)&src_addr);
                        if (new_session_id == 0) new_session_id = 1;

                        session = rel4u_session_allocate(&s->session_table, &src_addr, new_session_id, &was_evicted);
                        if (!session) {
                            /* Server Full and REJECT policy */
                            rel4u_header_t rej_hdr;
                            memset(&rej_hdr, 0, sizeof(rej_hdr));
                            rej_hdr.magic = REL4U_MAGIC;
                            rej_hdr.version = REL4U_VERSION;
                            rej_hdr.packet_type = REL4U_PKT_REJECT;
                            uint8_t rej_buf[REL4U_HEADER_LEN];
                            rel4u_packet_encode_header(&rej_hdr, rej_buf, sizeof(rej_buf));
                            rel4u_net_sendto(s->sock, rej_buf, sizeof(rej_buf), &src_addr);
                            rel4u_mutex_unlock(&s->table_mutex);
                            continue;
                        }
                    }

                    session->last_activity_ns = now_ns;
                    session->last_heartbeat_sent_ns = now_ns;
                    server_send_packet(s, session, REL4U_PKT_CONNECT_RESP, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                    rel4u_mutex_unlock(&s->table_mutex);
                    continue;
                }

                /* Look up session by address */
                rel4u_client_session_t* session = rel4u_session_find_by_addr(&s->session_table, &src_addr);
                if (!session || session->session_id != hdr.session_id) {
                    /* Reply with RESET to notify client its session was reset/purged */
                    if (hdr.packet_type != REL4U_PKT_RESET &&
                        hdr.packet_type != REL4U_PKT_DISCONNECT &&
                        hdr.packet_type != REL4U_PKT_REJECT) {
                        rel4u_header_t rst_hdr;
                        memset(&rst_hdr, 0, sizeof(rst_hdr));
                        rst_hdr.magic = REL4U_MAGIC;
                        rst_hdr.version = REL4U_VERSION;
                        rst_hdr.packet_type = REL4U_PKT_RESET;
                        rst_hdr.session_id = hdr.session_id;
                        rst_hdr.seq_num = hdr.seq_num;
                        uint8_t rst_buf[REL4U_HEADER_LEN];
                        rel4u_packet_encode_header(&rst_hdr, rst_buf, sizeof(rst_buf));
                        rel4u_net_sendto(s->sock, rst_buf, sizeof(rst_buf), &src_addr);
                    }
                    rel4u_mutex_unlock(&s->table_mutex);
                    continue;
                }

                rel4u_session_touch_lru(&s->session_table, session, now_ns);
                session->stats.packets_recv++;
                session->stats.bytes_recv += (uint64_t)bytes_read;

                /* Handle Connection ACK */
                if (hdr.packet_type == REL4U_PKT_CONNECT_ACK) {
                    session->state = REL4U_STATE_CONNECTED;
                    rel4u_mutex_unlock(&s->table_mutex);
                    continue;
                }

                /* Handle Disconnect (FIN) */
                if (hdr.packet_type == REL4U_PKT_DISCONNECT) {
                    rel4u_session_free(&s->session_table, session);
                    rel4u_mutex_unlock(&s->table_mutex);
                    continue;
                }

                /* Process SACK / ACK information */
                rel4u_send_window_on_ack(&session->send_win, hdr.ack_num, hdr.sack_mask, now_ns, &session->rtt, &session->stats);

                /* Handle Heartbeat */
                if (hdr.packet_type == REL4U_PKT_HEARTBEAT) {
                    server_send_packet(s, session, REL4U_PKT_HEARTBEAT_ACK, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                }

                /* Handle Data Packet */
                if (hdr.packet_type == REL4U_PKT_DATA) {
                    const uint8_t* payload = in_buf + REL4U_HEADER_LEN;
                    size_t ready_count = 0;
                    int res = rel4u_recv_window_on_packet(&session->recv_win, &hdr, payload,
                                                         ready_slots, 64, &ready_count);
                    if (res == 0) {
                        for (size_t k = 0; k < ready_count; k++) {
                            rel4u_mpmc_item_t item;
                            item.client_id = session->client_id;
                            item.mode = ready_slots[k].delivery_mode;
                            item.len = ready_slots[k].payload_len;
                            memcpy(item.data, ready_slots[k].payload, item.len);
                            rel4u_mpmc_push_notify(&s->recv_queue, &item);
                        }
                        session->ack_pending = true;
                    }
                }

                rel4u_mutex_unlock(&s->table_mutex);
            }
        }

        /* 3. Drain Outbound Send Queue with Window Flow Control */
        static rel4u_mpmc_item_t pending_item;
        static bool has_pending_item = false;

        while (true) {
            if (!has_pending_item) {
                if (!rel4u_mpmc_try_pop(&s->send_queue, &pending_item)) {
                    break; /* Queue empty */
                }
                has_pending_item = true;
            }

            rel4u_mutex_lock(&s->table_mutex);
            rel4u_client_session_t* session = rel4u_session_find_by_id(&s->session_table, pending_item.client_id);
            if (session && session->state == REL4U_STATE_CONNECTED) {
                if (rel4u_send_window_can_send(&session->send_win)) {
                    uint32_t seq = 0;
                    now_ns = rel4u_time_now_ns();
                    int push_res = rel4u_send_window_push(&session->send_win, (rel4u_delivery_mode_t)pending_item.mode,
                                                          pending_item.data, pending_item.len, now_ns, &seq);
                    if (push_res == 0) {
                        server_send_packet(s, session, REL4U_PKT_DATA, (rel4u_delivery_mode_t)pending_item.mode,
                                           seq, pending_item.data, pending_item.len);
                    }
                    has_pending_item = false; /* Successfully sent */
                    rel4u_mutex_unlock(&s->table_mutex);
                } else {
                    /* Window full for this client, pause drain until ACKs advance window */
                    rel4u_mutex_unlock(&s->table_mutex);
                    break;
                }
            } else {
                /* Client disconnected / invalid, discard item */
                has_pending_item = false;
                rel4u_mutex_unlock(&s->table_mutex);
            }
        }

        /* 4. Timers, Retransmissions, Keepalives & Inactivity Timeouts */
        now_ns = rel4u_time_now_ns();
        rel4u_mutex_lock(&s->table_mutex);

        for (uint32_t i = 0; i < s->session_table.max_clients; i++) {
            rel4u_client_session_t* session = &s->session_table.sessions[i];
            if (session->state == REL4U_STATE_DISCONNECTED) continue;

            /* Check Inactivity Timeout */
            if (now_ns - session->last_activity_ns >= (uint64_t)s->config.inactivity_timeout_ms * 1000000ULL) {
                rel4u_session_free(&s->session_table, session);
                continue;
            }

            if (session->state != REL4U_STATE_CONNECTED) continue;

            uint32_t rto_ms = rel4u_rtt_get_rto(&session->rtt);

            /* Expired Retransmissions */
            rel4u_send_slot_t* expired[16];
            size_t exp_count = rel4u_send_window_get_expired(&session->send_win, now_ns, rto_ms, expired, 16);
            if (exp_count > 0) {
                rel4u_rtt_on_timeout(&session->rtt);
                session->stats.packets_retransmitted += exp_count;
                session->stats.packets_lost += exp_count;

                for (size_t e = 0; e < exp_count; e++) {
                    expired[e]->last_sent_time_ns = now_ns;
                    expired[e]->retransmit_count++;
                    server_send_packet(s, session, REL4U_PKT_DATA, (rel4u_delivery_mode_t)expired[e]->delivery_mode,
                                       expired[e]->seq_num, expired[e]->payload, expired[e]->payload_len);
                }
            }

            /* Standalone ACK */
            if (session->ack_pending) {
                server_send_packet(s, session, REL4U_PKT_ACK, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
            }

            /* Heartbeat */
            if (now_ns - session->last_heartbeat_sent_ns >= (uint64_t)s->config.heartbeat_interval_ms * 1000000ULL) {
                server_send_packet(s, session, REL4U_PKT_HEARTBEAT, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                session->last_heartbeat_sent_ns = now_ns;
            }
        }

        rel4u_mutex_unlock(&s->table_mutex);
    }

    return NULL;
}
