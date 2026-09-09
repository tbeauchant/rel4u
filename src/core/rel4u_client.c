#include "../../include/rel4u.h"
#include "../platform/rel4u_net.h"
#include "../platform/rel4u_threads.h"
#include "../platform/rel4u_time.h"
#include "rel4u_mpmc.h"
#include "rel4u_packet.h"
#include "rel4u_window.h"
#include "rel4u_rtt.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

struct rel4u_client {
    rel4u_client_config_t config;
    rel4u_socket_t        sock;
    rel4u_net_addr_t      server_addr;
    rel4u_wakeup_pipe_t   wakeup_pipe;
    rel4u_thread_t        worker_thread;
    atomic_bool           running;
    atomic_int            state;          /* rel4u_conn_state_t */
    uint32_t              session_id;

    rel4u_mpmc_queue_t    send_queue;
    rel4u_mpmc_queue_t    recv_queue;
    rel4u_send_window_t   send_win;
    rel4u_recv_window_t   recv_win;
    rel4u_rtt_t           rtt;

    uint64_t              last_activity_ns;
    uint64_t              last_heartbeat_sent_ns;
    uint64_t              last_ack_sent_ns;
    bool                  ack_pending;

    rel4u_mutex_t         stats_mutex;
    rel4u_stats_t         stats;
};

static void* client_worker_thread_func(void* arg);

rel4u_client_t* rel4u_client_create(const rel4u_client_config_t* config) {
    if (!config || !config->server_address || config->server_port == 0) {
        return NULL;
    }

    rel4u_client_t* c = (rel4u_client_t*)calloc(1, sizeof(rel4u_client_t));
    if (!c) return NULL;

    c->config = *config;
    if (c->config.mtu == 0) c->config.mtu = REL4U_DEFAULT_MTU;
    if (c->config.send_queue_capacity < 8) c->config.send_queue_capacity = 128;
    if (c->config.recv_queue_capacity < 8) c->config.recv_queue_capacity = 128;
    if (c->config.window_size < 8) c->config.window_size = 64;
    if (c->config.heartbeat_interval_ms == 0) c->config.heartbeat_interval_ms = 1000;
    if (c->config.inactivity_timeout_ms == 0) c->config.inactivity_timeout_ms = 5000;
    if (c->config.connect_timeout_ms == 0) c->config.connect_timeout_ms = 3000;
    if (c->config.reconnect_interval_ms == 0) c->config.reconnect_interval_ms = 1000;

    c->sock = REL4U_INVALID_SOCKET;
    atomic_store(&c->state, REL4U_STATE_DISCONNECTED);
    atomic_store(&c->running, false);

    if (c->config.memory_mode == REL4U_MEM_DYNAMIC) {
        if (c->config.max_send_memory_bytes == 0) c->config.max_send_memory_bytes = 1024 * 1024;
        if (c->config.max_recv_memory_bytes == 0) c->config.max_recv_memory_bytes = 1024 * 1024;
    }

    if (rel4u_mpmc_init(&c->send_queue, c->config.memory_mode, c->config.queue_full_policy,
                        c->config.send_queue_capacity, c->config.max_send_memory_bytes) != 0) {
        free(c);
        return NULL;
    }

    if (rel4u_mpmc_init(&c->recv_queue, c->config.memory_mode, c->config.queue_full_policy,
                        c->config.recv_queue_capacity, c->config.max_recv_memory_bytes) != 0) {
        rel4u_mpmc_destroy(&c->send_queue);
        free(c);
        return NULL;
    }

    if (rel4u_send_window_init(&c->send_win, c->config.window_size, 1) != 0) {
        rel4u_mpmc_destroy(&c->recv_queue);
        rel4u_mpmc_destroy(&c->send_queue);
        free(c);
        return NULL;
    }

    if (rel4u_recv_window_init(&c->recv_win, c->config.window_size, 1) != 0) {
        rel4u_send_window_destroy(&c->send_win);
        rel4u_mpmc_destroy(&c->recv_queue);
        rel4u_mpmc_destroy(&c->send_queue);
        free(c);
        return NULL;
    }

    rel4u_rtt_init(&c->rtt, REL4U_RTT_DEFAULT_RTO_MS, REL4U_RTT_MIN_RTO_MS, REL4U_RTT_MAX_RTO_MS);
    rel4u_mutex_init(&c->stats_mutex);

    return c;
}

int rel4u_client_connect(rel4u_client_t* c) {
    if (!c) return REL4U_ERR_INVALID_PARAM;
    if (atomic_load(&c->state) != REL4U_STATE_DISCONNECTED) {
        return REL4U_OK; /* Already connecting or connected */
    }

    rel4u_net_init();

    if (rel4u_net_addr_from_string(&c->server_addr, c->config.server_address, c->config.server_port) != 0) {
        return REL4U_ERR_INVALID_PARAM;
    }

    c->sock = rel4u_net_socket_create_udp(c->server_addr.addr.ss_family == AF_INET6);
    if (c->sock == REL4U_INVALID_SOCKET) {
        return REL4U_ERR_SOCKET;
    }

    if (c->config.bind_address || c->config.bind_port != 0) {
        rel4u_net_addr_t bind_addr;
        if (rel4u_net_addr_from_string(&bind_addr, c->config.bind_address ? c->config.bind_address : "0.0.0.0", c->config.bind_port) == 0) {
            rel4u_net_socket_bind(c->sock, &bind_addr);
        }
    }

    if (rel4u_wakeup_pipe_create(&c->wakeup_pipe) != 0) {
        rel4u_net_socket_close(c->sock);
        c->sock = REL4U_INVALID_SOCKET;
        return REL4U_ERR_SYSTEM;
    }

    atomic_store(&c->state, REL4U_STATE_CONNECTING);
    atomic_store(&c->running, true);

    if (rel4u_thread_create(&c->worker_thread, client_worker_thread_func, c) != 0) {
        atomic_store(&c->running, false);
        atomic_store(&c->state, REL4U_STATE_DISCONNECTED);
        rel4u_wakeup_pipe_close(&c->wakeup_pipe);
        rel4u_net_socket_close(c->sock);
        c->sock = REL4U_INVALID_SOCKET;
        return REL4U_ERR_SYSTEM;
    }

    /* Wait for connection to establish or timeout */
    uint64_t start_ms = rel4u_time_now_ms();
    while (atomic_load(&c->running)) {
        int state = atomic_load(&c->state);
        if (state == REL4U_STATE_CONNECTED) {
            return REL4U_OK;
        }
        if (state == REL4U_STATE_DISCONNECTED) {
            return REL4U_ERR_NOT_CONNECTED;
        }
        if (rel4u_time_now_ms() - start_ms >= c->config.connect_timeout_ms) {
            rel4u_client_disconnect(c);
            return REL4U_ERR_TIMEOUT;
        }
        rel4u_time_sleep_ms(10);
    }

    return REL4U_ERR_NOT_CONNECTED;
}

int rel4u_client_disconnect(rel4u_client_t* c) {
    if (!c) return REL4U_ERR_INVALID_PARAM;
    if (!atomic_load(&c->running)) return REL4U_OK;

    atomic_store(&c->state, REL4U_STATE_DISCONNECTING);
    rel4u_wakeup_pipe_signal(&c->wakeup_pipe);

    /* Send FIN packet */
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = REL4U_PKT_DISCONNECT;
    hdr.session_id = c->session_id;

    uint8_t buf[REL4U_HEADER_LEN];
    rel4u_packet_encode_header(&hdr, buf, sizeof(buf));
    rel4u_net_sendto(c->sock, buf, sizeof(buf), &c->server_addr);

    atomic_store(&c->running, false);
    rel4u_wakeup_pipe_signal(&c->wakeup_pipe);
    rel4u_mpmc_wake_all(&c->recv_queue);
    rel4u_thread_join(c->worker_thread);

    rel4u_wakeup_pipe_close(&c->wakeup_pipe);
    rel4u_net_socket_close(c->sock);
    c->sock = REL4U_INVALID_SOCKET;
    atomic_store(&c->state, REL4U_STATE_DISCONNECTED);

    return REL4U_OK;
}

void rel4u_client_destroy(rel4u_client_t* c) {
    if (!c) return;
    rel4u_client_disconnect(c);

    rel4u_mpmc_destroy(&c->send_queue);
    rel4u_mpmc_destroy(&c->recv_queue);
    rel4u_send_window_destroy(&c->send_win);
    rel4u_recv_window_destroy(&c->recv_win);
    rel4u_mutex_destroy(&c->stats_mutex);

    free(c);
}

int rel4u_client_send(rel4u_client_t* c, rel4u_delivery_mode_t mode, const void* data, size_t len) {
    if (!c || !data) return REL4U_ERR_INVALID_PARAM;
    if (len > REL4U_MAX_PAYLOAD(c->config.mtu)) return REL4U_ERR_MSG_TOO_LARGE;

    if (!atomic_load(&c->running)) return REL4U_ERR_NOT_CONNECTED;
    int state = atomic_load(&c->state);
    if (state != REL4U_STATE_CONNECTED) {
        if (c->config.disable_auto_reconnect || state != REL4U_STATE_CONNECTING) {
            return REL4U_ERR_NOT_CONNECTED;
        }
    }

    rel4u_mpmc_item_t item;
    item.client_id = 0;
    item.mode = (uint8_t)mode;
    item.len = (uint16_t)len;
    memcpy(item.data, data, len);

    if (!rel4u_mpmc_try_push(&c->send_queue, &item)) {
        rel4u_mutex_lock(&c->stats_mutex);
        c->stats.packets_dropped_queue_full = rel4u_mpmc_dropped_count(&c->send_queue) +
                                              rel4u_mpmc_dropped_count(&c->recv_queue);
        rel4u_mutex_unlock(&c->stats_mutex);
        return REL4U_ERR_QUEUE_FULL;
    }

    rel4u_wakeup_pipe_signal(&c->wakeup_pipe);
    return REL4U_OK;
}

int rel4u_client_recv(rel4u_client_t* c, void* buffer, size_t buffer_size, size_t* out_len, int32_t timeout_ms) {
    if (!c || !buffer || buffer_size == 0) return REL4U_ERR_INVALID_PARAM;

    rel4u_mpmc_item_t item;
    if (!rel4u_mpmc_pop_wait(&c->recv_queue, &item, timeout_ms)) {
        return (timeout_ms == 0) ? REL4U_ERR_QUEUE_EMPTY : REL4U_ERR_TIMEOUT;
    }

    size_t copy_len = (item.len <= buffer_size) ? item.len : buffer_size;
    memcpy(buffer, item.data, copy_len);
    if (out_len) *out_len = copy_len;

    return REL4U_OK;
}

rel4u_conn_state_t rel4u_client_get_state(rel4u_client_t* c) {
    return c ? (rel4u_conn_state_t)atomic_load(&c->state) : REL4U_STATE_DISCONNECTED;
}

int rel4u_client_get_stats(rel4u_client_t* c, rel4u_stats_t* out_stats) {
    if (!c || !out_stats) return REL4U_ERR_INVALID_PARAM;
    rel4u_mutex_lock(&c->stats_mutex);
    *out_stats = c->stats;
    out_stats->packets_dropped_queue_full = rel4u_mpmc_dropped_count(&c->send_queue) +
                                          rel4u_mpmc_dropped_count(&c->recv_queue);
    out_stats->send_queue_occupancy = (uint32_t)rel4u_mpmc_size(&c->send_queue);
    out_stats->recv_queue_occupancy = (uint32_t)rel4u_mpmc_size(&c->recv_queue);
    out_stats->send_queue_bytes = rel4u_mpmc_bytes(&c->send_queue);
    out_stats->recv_queue_bytes = rel4u_mpmc_bytes(&c->recv_queue);
    out_stats->current_rto_ms = rel4u_rtt_get_rto(&c->rtt);
    rel4u_mutex_unlock(&c->stats_mutex);
    return REL4U_OK;
}

/* Helper to send standalone packet */
static void client_send_packet(rel4u_client_t* c, rel4u_pkt_type_t type, rel4u_delivery_mode_t mode,
                               uint32_t seq, const void* payload, size_t payload_len) {
    rel4u_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = REL4U_MAGIC;
    hdr.version = REL4U_VERSION;
    hdr.packet_type = (uint8_t)type;
    hdr.delivery_mode = (uint8_t)mode;
    hdr.payload_len = (uint16_t)payload_len;
    hdr.session_id = c->session_id;
    hdr.seq_num = seq;

    rel4u_recv_window_get_ack_info(&c->recv_win, &hdr.ack_num, &hdr.sack_mask);

    uint8_t raw_buf[REL4U_DEFAULT_MTU];
    int hdr_len = rel4u_packet_encode_header(&hdr, raw_buf, sizeof(raw_buf));
    if (hdr_len <= 0) return;

    if (payload_len > 0 && payload) {
        memcpy(raw_buf + hdr_len, payload, payload_len);
    }

    size_t total_len = (size_t)hdr_len + payload_len;
    int sent = rel4u_net_sendto(c->sock, raw_buf, total_len, &c->server_addr);
    if (sent > 0) {
        rel4u_mutex_lock(&c->stats_mutex);
        c->stats.packets_sent++;
        c->stats.bytes_sent += (uint64_t)sent;
        rel4u_mutex_unlock(&c->stats_mutex);
        c->last_ack_sent_ns = rel4u_time_now_ns();
        c->ack_pending = false;
    }
}

static void client_trigger_reconnect(rel4u_client_t* c) {
    if (c->config.disable_auto_reconnect) {
        atomic_store(&c->state, REL4U_STATE_DISCONNECTED);
        return;
    }
    /* Reclaim unacked reliable packets from send window to send queue */
    rel4u_send_window_reclaim_unacked(&c->send_win, &c->send_queue);
    /* Reset sliding windows */
    rel4u_send_window_reset(&c->send_win, 1);
    rel4u_recv_window_reset(&c->recv_win, 1);
    rel4u_rtt_init(&c->rtt, REL4U_RTT_DEFAULT_RTO_MS, REL4U_RTT_MIN_RTO_MS, REL4U_RTT_MAX_RTO_MS);
    c->session_id = 0;
    c->ack_pending = false;
    c->last_activity_ns = rel4u_time_now_ns();
    atomic_store(&c->state, REL4U_STATE_CONNECTING);
}

static void* client_worker_thread_func(void* arg) {
    rel4u_client_t* c = (rel4u_client_t*)arg;
    uint8_t in_buf[REL4U_DEFAULT_MTU];
    rel4u_recv_slot_t ready_slots[64];

    c->last_activity_ns = rel4u_time_now_ns();
    c->last_heartbeat_sent_ns = rel4u_time_now_ns();
    c->last_ack_sent_ns = rel4u_time_now_ns();

    uint64_t last_connect_req_ns = 0;

    while (atomic_load(&c->running)) {
        uint64_t now_ns = rel4u_time_now_ns();
        int state = atomic_load(&c->state);

        /* 1. Handshake logic if CONNECTING */
        if (state == REL4U_STATE_CONNECTING) {
            uint32_t retry_ms = 200;
            if (c->config.reconnect_interval_ms > 0 && c->config.reconnect_interval_ms < 200) {
                retry_ms = c->config.reconnect_interval_ms;
            }
            uint64_t retry_interval_ns = (uint64_t)retry_ms * 1000000ULL;
            if (now_ns - last_connect_req_ns >= retry_interval_ns) {
                client_send_packet(c, REL4U_PKT_CONNECT_REQ, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                last_connect_req_ns = now_ns;
            }
        }

        /* 2. Poll socket and wakeup pipe */
        bool sock_read = false, pipe_read = false;
        rel4u_net_poll(c->sock, &c->wakeup_pipe, 10, &sock_read, &pipe_read);

        if (pipe_read) {
            rel4u_wakeup_pipe_drain(&c->wakeup_pipe);
        }

        /* 3. Handle incoming socket datagrams */
        if (sock_read) {
            rel4u_net_addr_t src_addr;
            int bytes_read;
            while ((bytes_read = rel4u_net_recvfrom(c->sock, in_buf, sizeof(in_buf), &src_addr)) > 0) {
                rel4u_header_t hdr;
                if (rel4u_packet_decode_header(in_buf, (size_t)bytes_read, &hdr) != 0) {
                    continue; /* Invalid packet, ignore */
                }

                now_ns = rel4u_time_now_ns();
                c->last_activity_ns = now_ns;

                rel4u_mutex_lock(&c->stats_mutex);
                c->stats.packets_recv++;
                c->stats.bytes_recv += (uint64_t)bytes_read;
                rel4u_mutex_unlock(&c->stats_mutex);

                /* Handle Connection Response (SYN-ACK) */
                if (hdr.packet_type == REL4U_PKT_CONNECT_RESP && state == REL4U_STATE_CONNECTING) {
                    c->session_id = hdr.session_id;
                    atomic_store(&c->state, REL4U_STATE_CONNECTED);
                    client_send_packet(c, REL4U_PKT_CONNECT_ACK, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                    state = REL4U_STATE_CONNECTED;
                    c->last_activity_ns = now_ns;
                    c->last_heartbeat_sent_ns = now_ns;
                    continue;
                }

                /* Handle Reset */
                if (hdr.packet_type == REL4U_PKT_RESET) {
                    client_trigger_reconnect(c);
                    state = atomic_load(&c->state);
                    continue;
                }

                /* Voluntary Disconnect / Server Reject: Permanently disconnect without auto-reconnecting */
                if (hdr.packet_type == REL4U_PKT_REJECT || hdr.packet_type == REL4U_PKT_DISCONNECT) {
                    atomic_store(&c->state, REL4U_STATE_DISCONNECTED);
                    break;
                }

                /* Verify session id for connected packets */
                if (state == REL4U_STATE_CONNECTED && hdr.session_id != c->session_id) {
                    continue;
                }

                /* Process cumulative / SACK info on all incoming packets */
                rel4u_send_window_on_ack(&c->send_win, hdr.ack_num, hdr.sack_mask, now_ns, &c->rtt, &c->stats);

                /* Handle Heartbeat */
                if (hdr.packet_type == REL4U_PKT_HEARTBEAT) {
                    client_send_packet(c, REL4U_PKT_HEARTBEAT_ACK, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                }

                /* Handle Data Packet */
                if (hdr.packet_type == REL4U_PKT_DATA) {
                    const uint8_t* payload = in_buf + REL4U_HEADER_LEN;
                    size_t ready_count = 0;
                    int res = rel4u_recv_window_on_packet(&c->recv_win, &hdr, payload,
                                                         ready_slots, 64, &ready_count);
                    if (res == 0) {
                        for (size_t k = 0; k < ready_count; k++) {
                            rel4u_mpmc_item_t item;
                            item.client_id = 0;
                            item.mode = ready_slots[k].delivery_mode;
                            item.len = ready_slots[k].payload_len;
                            memcpy(item.data, ready_slots[k].payload, item.len);
                            rel4u_mpmc_push_notify(&c->recv_queue, &item);
                        }
                        c->ack_pending = true;
                    }
                }
            }
        }

        /* 4. Drain Send Queue into Send Window & Socket */
        if (state == REL4U_STATE_CONNECTED) {
            rel4u_mpmc_item_t item;
            while (rel4u_send_window_can_send(&c->send_win) && rel4u_mpmc_try_pop(&c->send_queue, &item)) {
                uint32_t seq = 0;
                now_ns = rel4u_time_now_ns();
                int push_res = rel4u_send_window_push(&c->send_win, (rel4u_delivery_mode_t)item.mode,
                                                      item.data, item.len, now_ns, &seq);
                if (push_res == 0) {
                    client_send_packet(c, REL4U_PKT_DATA, (rel4u_delivery_mode_t)item.mode, seq, item.data, item.len);
                }
            }
        }

        /* 5. Timers & Retransmissions */
        if (state == REL4U_STATE_CONNECTED) {
            now_ns = rel4u_time_now_ns();
            uint32_t rto_ms = rel4u_rtt_get_rto(&c->rtt);

            /* Check expired packets */
            rel4u_send_slot_t* expired[16];
            size_t exp_count = rel4u_send_window_get_expired(&c->send_win, now_ns, rto_ms, expired, 16);
            if (exp_count > 0) {
                rel4u_rtt_on_timeout(&c->rtt);
                rel4u_mutex_lock(&c->stats_mutex);
                c->stats.packets_retransmitted += exp_count;
                c->stats.packets_lost += exp_count;
                rel4u_mutex_unlock(&c->stats_mutex);

                for (size_t e = 0; e < exp_count; e++) {
                    expired[e]->last_sent_time_ns = now_ns;
                    expired[e]->retransmit_count++;
                    client_send_packet(c, REL4U_PKT_DATA, (rel4u_delivery_mode_t)expired[e]->delivery_mode,
                                       expired[e]->seq_num, expired[e]->payload, expired[e]->payload_len);
                }
            }

            /* Send standalone ACK if pending */
            if (c->ack_pending) {
                client_send_packet(c, REL4U_PKT_ACK, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
            }

            /* Heartbeat timer */
            if (now_ns - c->last_heartbeat_sent_ns >= (uint64_t)c->config.heartbeat_interval_ms * 1000000ULL) {
                client_send_packet(c, REL4U_PKT_HEARTBEAT, REL4U_MODE_UNRELIABLE_UNORDERED, 0, NULL, 0);
                c->last_heartbeat_sent_ns = now_ns;
            }

            /* Inactivity timeout */
            if (now_ns - c->last_activity_ns >= (uint64_t)c->config.inactivity_timeout_ms * 1000000ULL) {
                if (c->config.disable_auto_reconnect) {
                    atomic_store(&c->state, REL4U_STATE_DISCONNECTED);
                } else {
                    client_trigger_reconnect(c);
                }
            }
        }
    }

    return NULL;
}
