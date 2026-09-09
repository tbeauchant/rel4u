#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE
#include "network_sim.h"
#include "../../src/platform/rel4u_net.h"
#include "../../src/platform/rel4u_threads.h"
#include "../../src/platform/rel4u_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIM_CLIENTS 64
#define MAX_DELAYED_PACKETS 1024
#define MAX_PACKET_SIZE 2048

typedef struct sim_delayed_packet {
    bool             in_use;
    uint64_t         delivery_time_ms;
    rel4u_socket_t   send_sock;
    rel4u_net_addr_t dest_addr;
    uint8_t          data[MAX_PACKET_SIZE];
    size_t           len;
} sim_delayed_packet_t;

typedef struct sim_client_session {
    bool             active;
    rel4u_net_addr_t client_addr;
    rel4u_socket_t   worker_sock;
    uint32_t         c2s_packet_count;
    uint32_t         s2c_packet_count;
    uint64_t         last_active_ms;
} sim_client_session_t;

struct rel4u_net_sim {
    uint16_t               proxy_port;
    uint16_t               target_server_port;
    rel4u_net_addr_t       target_server_addr;

    rel4u_socket_t         proxy_listen_sock;
    rel4u_wakeup_pipe_t    wakeup_pipe;

    rel4u_thread_t         worker_thread;
    rel4u_mutex_t          mutex;
    bool                   running;
    uint32_t               prng_state;

    rel4u_net_sim_config_t config;
    rel4u_net_sim_stats_t  stats;

    sim_client_session_t   clients[MAX_SIM_CLIENTS];
    sim_delayed_packet_t   delayed_queue[MAX_DELAYED_PACKETS];
};

static uint32_t sim_rand(rel4u_net_sim_t* sim) {
    sim->prng_state ^= (sim->prng_state << 13);
    sim->prng_state ^= (sim->prng_state >> 17);
    sim->prng_state ^= (sim->prng_state << 5);
    return sim->prng_state;
}

static double sim_rand_float(rel4u_net_sim_t* sim) {
    return (double)(sim_rand(sim) & 0x7FFFFFFF) / (double)0x7FFFFFFF;
}

static int sim_find_or_create_client(rel4u_net_sim_t* sim, const rel4u_net_addr_t* addr) {
    int free_slot = -1;
    for (int i = 0; i < MAX_SIM_CLIENTS; i++) {
        if (sim->clients[i].active) {
            if (rel4u_net_addr_equal(&sim->clients[i].client_addr, addr)) {
                sim->clients[i].last_active_ms = rel4u_time_now_ms();
                return i;
            }
        } else if (free_slot == -1) {
            free_slot = i;
        }
    }

    if (free_slot != -1) {
        rel4u_socket_t sock = rel4u_net_socket_create_udp(false);
        if (sock == REL4U_INVALID_SOCKET) {
            return -1;
        }
        rel4u_net_addr_t bind_addr;
        if (rel4u_net_addr_from_string(&bind_addr, "127.0.0.1", 0) != 0 ||
            rel4u_net_socket_bind(sock, &bind_addr) != 0) {
            rel4u_net_socket_close(sock);
            return -1;
        }

        sim->clients[free_slot].active = true;
        sim->clients[free_slot].client_addr = *addr;
        sim->clients[free_slot].worker_sock = sock;
        sim->clients[free_slot].c2s_packet_count = 0;
        sim->clients[free_slot].s2c_packet_count = 0;
        sim->clients[free_slot].last_active_ms = rel4u_time_now_ms();
        return free_slot;
    }
    return -1;
}

static bool sim_enqueue_delayed(rel4u_net_sim_t* sim, rel4u_socket_t sock, const rel4u_net_addr_t* dest,
                                const uint8_t* data, size_t len, uint64_t delivery_time_ms) {
    if (len > MAX_PACKET_SIZE) return false;
    for (int i = 0; i < MAX_DELAYED_PACKETS; i++) {
        if (!sim->delayed_queue[i].in_use) {
            sim->delayed_queue[i].in_use = true;
            sim->delayed_queue[i].delivery_time_ms = delivery_time_ms;
            sim->delayed_queue[i].send_sock = sock;
            sim->delayed_queue[i].dest_addr = *dest;
            memcpy(sim->delayed_queue[i].data, data, len);
            sim->delayed_queue[i].len = len;
            return true;
        }
    }
    return false;
}

static void sim_flush_delayed_packets(rel4u_net_sim_t* sim, uint64_t now_ms) {
    for (int i = 0; i < MAX_DELAYED_PACKETS; i++) {
        if (sim->delayed_queue[i].in_use && sim->delayed_queue[i].delivery_time_ms <= now_ms) {
            rel4u_net_sendto(sim->delayed_queue[i].send_sock,
                             sim->delayed_queue[i].data,
                             sim->delayed_queue[i].len,
                             &sim->delayed_queue[i].dest_addr);
            sim->delayed_queue[i].in_use = false;
        }
    }
}

static uint64_t sim_get_next_delayed_delivery_ms(rel4u_net_sim_t* sim) {
    uint64_t next_ms = UINT64_MAX;
    for (int i = 0; i < MAX_DELAYED_PACKETS; i++) {
        if (sim->delayed_queue[i].in_use) {
            if (sim->delayed_queue[i].delivery_time_ms < next_ms) {
                next_ms = sim->delayed_queue[i].delivery_time_ms;
            }
        }
    }
    return next_ms;
}

static void sim_process_packet(rel4u_net_sim_t* sim, bool is_client_to_server, int client_idx,
                               rel4u_socket_t send_sock, const rel4u_net_addr_t* dest_addr,
                               const uint8_t* data, size_t len) {
    if (is_client_to_server) {
        sim->stats.client_packets_in++;
        sim->clients[client_idx].c2s_packet_count++;
    } else {
        sim->stats.server_packets_in++;
        sim->clients[client_idx].s2c_packet_count++;
    }

    uint32_t packet_num = is_client_to_server ? sim->clients[client_idx].c2s_packet_count
                                              : sim->clients[client_idx].s2c_packet_count;

    // 1. Check Burst Drop
    if (sim->config.burst_drop_interval > 0 && sim->config.burst_drop_count > 0) {
        uint32_t pos = packet_num % sim->config.burst_drop_interval;
        if (pos < sim->config.burst_drop_count) {
            if (is_client_to_server) sim->stats.client_packets_dropped++;
            else sim->stats.server_packets_dropped++;
            return;
        }
    }

    // 2. Check Random Drop
    if (sim->config.drop_rate > 0.0) {
        if (sim_rand_float(sim) < sim->config.drop_rate) {
            if (is_client_to_server) sim->stats.client_packets_dropped++;
            else sim->stats.server_packets_dropped++;
            return;
        }
    }

    // 3. Compute Latency / Jitter
    uint32_t latency = sim->config.delay_min_ms;
    if (sim->config.delay_max_ms > sim->config.delay_min_ms) {
        latency += sim_rand(sim) % (sim->config.delay_max_ms - sim->config.delay_min_ms + 1);
    }

    // 4. Check Out-of-order Reordering
    if (sim->config.reorder_rate > 0.0) {
        if (sim_rand_float(sim) < sim->config.reorder_rate) {
            latency += 20 + (sim_rand(sim) % 25);
        }
    }

    // 5. Check Duplication
    bool duplicate = false;
    if (sim->config.duplicate_rate > 0.0) {
        if (sim_rand_float(sim) < sim->config.duplicate_rate) {
            duplicate = true;
            if (is_client_to_server) sim->stats.client_packets_duplicated++;
            else sim->stats.server_packets_duplicated++;
        }
    }

    uint64_t now_ms = rel4u_time_now_ms();
    if (latency == 0 && !duplicate) {
        rel4u_net_sendto(send_sock, data, len, dest_addr);
        if (is_client_to_server) sim->stats.client_packets_forwarded++;
        else sim->stats.server_packets_forwarded++;
    } else {
        sim_enqueue_delayed(sim, send_sock, dest_addr, data, len, now_ms + latency);
        if (duplicate) {
            sim_enqueue_delayed(sim, send_sock, dest_addr, data, len, now_ms + latency + 2);
        }
        if (is_client_to_server) sim->stats.client_packets_forwarded++;
        else sim->stats.server_packets_forwarded++;
    }
}

static void* sim_worker_thread_func(void* arg)
{
    rel4u_net_sim_t* sim = (rel4u_net_sim_t*)arg;
    uint8_t buffer[MAX_PACKET_SIZE];

    while (sim->running) {
        uint64_t now_ms = rel4u_time_now_ms();

        rel4u_mutex_lock(&sim->mutex);
        sim_flush_delayed_packets(sim, now_ms);

        uint64_t next_delayed = sim_get_next_delayed_delivery_ms(sim);
        int timeout_ms = 20;
        if (next_delayed != UINT64_MAX) {
            if (next_delayed <= now_ms) {
                timeout_ms = 0;
            } else {
                uint64_t diff = next_delayed - now_ms;
                timeout_ms = (diff < 20) ? (int)diff : 20;
            }
        }

#if defined(_WIN32)
        WSAPOLLFD fds[2 + MAX_SIM_CLIENTS];
#else
        struct pollfd fds[2 + MAX_SIM_CLIENTS];
#endif
        int client_map[2 + MAX_SIM_CLIENTS];
        int nfds = 0;

        // fd 0: listen sock
        fds[nfds].fd = sim->proxy_listen_sock;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        client_map[nfds] = -1;
        nfds++;

        // fd 1: wakeup pipe
#if defined(_WIN32)
        rel4u_socket_t pipe_fd = sim->wakeup_pipe.recv_sock;
#else
        int pipe_fd = sim->wakeup_pipe.read_fd;
#endif
        if (pipe_fd != -1 && pipe_fd != REL4U_INVALID_SOCKET) {
            fds[nfds].fd = pipe_fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            client_map[nfds] = -2;
            nfds++;
        }

        // fds for clients
        for (int i = 0; i < MAX_SIM_CLIENTS; i++) {
            if (sim->clients[i].active && sim->clients[i].worker_sock != REL4U_INVALID_SOCKET) {
                fds[nfds].fd = sim->clients[i].worker_sock;
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                client_map[nfds] = i;
                nfds++;
            }
        }
        rel4u_mutex_unlock(&sim->mutex);

#if defined(_WIN32)
        int poll_res = WSAPoll(fds, nfds, timeout_ms);
#else
        int poll_res = poll(fds, nfds, timeout_ms);
#endif

        if (poll_res > 0) {
            rel4u_mutex_lock(&sim->mutex);
            for (int i = 0; i < nfds; i++) {
                if (!(fds[i].revents & POLLIN)) continue;

                if (client_map[i] == -1) {
                    // Client -> Server on proxy_listen_sock
                    rel4u_net_addr_t src_addr;
                    int n = rel4u_net_recvfrom(sim->proxy_listen_sock, buffer, sizeof(buffer), &src_addr);
                    if (n > 0) {
                        int c_idx = sim_find_or_create_client(sim, &src_addr);
                        if (c_idx >= 0) {
                            sim_process_packet(sim, true, c_idx, sim->clients[c_idx].worker_sock,
                                               &sim->target_server_addr, buffer, (size_t)n);
                        }
                    }
                } else if (client_map[i] == -2) {
                    // Wakeup pipe
                    rel4u_wakeup_pipe_drain(&sim->wakeup_pipe);
                } else {
                    // Server -> Client on specific worker_sock
                    int c_idx = client_map[i];
                    if (c_idx >= 0 && sim->clients[c_idx].active) {
                        rel4u_net_addr_t src_addr;
                        int n = rel4u_net_recvfrom(sim->clients[c_idx].worker_sock, buffer, sizeof(buffer), &src_addr);
                        if (n > 0) {
                            sim_process_packet(sim, false, c_idx, sim->proxy_listen_sock,
                                               &sim->clients[c_idx].client_addr, buffer, (size_t)n);
                        }
                    }
                }
            }
            rel4u_mutex_unlock(&sim->mutex);
        }
    }

    return 0;
}

rel4u_net_sim_t* rel4u_net_sim_create(uint16_t proxy_port, uint16_t target_server_port, const rel4u_net_sim_config_t* config) {
    rel4u_net_init();

    rel4u_net_sim_t* sim = (rel4u_net_sim_t*)calloc(1, sizeof(rel4u_net_sim_t));
    if (!sim) return NULL;

    sim->proxy_port = proxy_port;
    sim->target_server_port = target_server_port;
    sim->prng_state = 123456789u + (uint32_t)rel4u_time_now_ns();
    if (config) {
        sim->config = *config;
    }

    if (rel4u_net_addr_from_string(&sim->target_server_addr, "127.0.0.1", target_server_port) != 0) {
        free(sim);
        return NULL;
    }

    sim->proxy_listen_sock = rel4u_net_socket_create_udp(false);
    if (sim->proxy_listen_sock == REL4U_INVALID_SOCKET) {
        free(sim);
        return NULL;
    }

    rel4u_net_addr_t bind_addr;
    if (rel4u_net_addr_from_string(&bind_addr, "127.0.0.1", proxy_port) != 0 ||
        rel4u_net_socket_set_reuseaddr(sim->proxy_listen_sock, true) != 0 ||
        rel4u_net_socket_bind(sim->proxy_listen_sock, &bind_addr) != 0) {
        rel4u_net_socket_close(sim->proxy_listen_sock);
        free(sim);
        return NULL;
    }

    if (rel4u_wakeup_pipe_create(&sim->wakeup_pipe) != 0) {
        rel4u_net_socket_close(sim->proxy_listen_sock);
        free(sim);
        return NULL;
    }

    rel4u_mutex_init(&sim->mutex);
    return sim;
}

int rel4u_net_sim_start(rel4u_net_sim_t* sim) {
    if (!sim) return -1;
    sim->running = true;
    if (rel4u_thread_create(&sim->worker_thread, sim_worker_thread_func, sim) != 0) {
        sim->running = false;
        return -1;
    }
    return 0;
}

void rel4u_net_sim_set_config(rel4u_net_sim_t* sim, const rel4u_net_sim_config_t* config) {
    if (!sim || !config) return;
    rel4u_mutex_lock(&sim->mutex);
    sim->config = *config;
    rel4u_mutex_unlock(&sim->mutex);
    rel4u_wakeup_pipe_signal(&sim->wakeup_pipe);
}

void rel4u_net_sim_get_stats(rel4u_net_sim_t* sim, rel4u_net_sim_stats_t* out_stats) {
    if (!sim || !out_stats) return;
    rel4u_mutex_lock(&sim->mutex);
    *out_stats = sim->stats;
    rel4u_mutex_unlock(&sim->mutex);
}

void rel4u_net_sim_stop(rel4u_net_sim_t* sim) {
    if (!sim || !sim->running) return;
    sim->running = false;
    rel4u_wakeup_pipe_signal(&sim->wakeup_pipe);
    rel4u_thread_join(sim->worker_thread);
}

void rel4u_net_sim_destroy(rel4u_net_sim_t* sim) {
    if (!sim) return;
    if (sim->running) {
        rel4u_net_sim_stop(sim);
    }
    rel4u_wakeup_pipe_close(&sim->wakeup_pipe);
    rel4u_net_socket_close(sim->proxy_listen_sock);

    for (int i = 0; i < MAX_SIM_CLIENTS; i++) {
        if (sim->clients[i].active && sim->clients[i].worker_sock != REL4U_INVALID_SOCKET) {
            rel4u_net_socket_close(sim->clients[i].worker_sock);
        }
    }
    rel4u_mutex_destroy(&sim->mutex);
    free(sim);
}
