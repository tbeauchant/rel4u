/**
 * @file network_sim.h
 * @brief UDP Network Simulation Proxy for rel4u testing.
 *
 * Provides in-process transparent UDP proxying with configurable packet drops,
 * burst drops, latency/jitter, packet duplication, and out-of-order reordering.
 */

#ifndef NETWORK_SIM_H
#define NETWORK_SIM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rel4u_net_sim_config {
    double   drop_rate;            /**< 0.0 to 1.0: Random packet drop probability */
    uint32_t burst_drop_count;     /**< Number of consecutive packets to drop during burst */
    uint32_t burst_drop_interval;  /**< Trigger burst drop every N packets (0 = disabled) */
    uint32_t delay_min_ms;         /**< Minimum latency in ms */
    uint32_t delay_max_ms;         /**< Maximum latency in ms (delay_max_ms >= delay_min_ms) */
    double   duplicate_rate;       /**< 0.0 to 1.0: Packet duplication probability */
    double   reorder_rate;         /**< 0.0 to 1.0: Packet out-of-order reordering probability */
} rel4u_net_sim_config_t;

typedef struct rel4u_net_sim_stats {
    uint64_t client_packets_in;
    uint64_t client_packets_forwarded;
    uint64_t client_packets_dropped;
    uint64_t client_packets_duplicated;
    uint64_t server_packets_in;
    uint64_t server_packets_forwarded;
    uint64_t server_packets_dropped;
    uint64_t server_packets_duplicated;
} rel4u_net_sim_stats_t;

typedef struct rel4u_net_sim rel4u_net_sim_t;

/**
 * @brief Create a network simulation proxy.
 *
 * @param proxy_port Port the proxy listens on for client packets.
 * @param target_server_port Port of the actual rel4u server.
 * @param config Initial simulation impairment configuration.
 * @return Proxy instance handle, or NULL on error.
 */
rel4u_net_sim_t* rel4u_net_sim_create(uint16_t proxy_port, uint16_t target_server_port, const rel4u_net_sim_config_t* config);

/**
 * @brief Start the proxy background worker thread.
 * @param sim Proxy handle.
 * @return 0 on success, or negative error code.
 */
int rel4u_net_sim_start(rel4u_net_sim_t* sim);

/**
 * @brief Dynamically update simulation parameters while running.
 * @param sim Proxy handle.
 * @param config New configuration.
 */
void rel4u_net_sim_set_config(rel4u_net_sim_t* sim, const rel4u_net_sim_config_t* config);

/**
 * @brief Retrieve current simulation statistics.
 * @param sim Proxy handle.
 * @param out_stats Pointer to destination stats struct.
 */
void rel4u_net_sim_get_stats(rel4u_net_sim_t* sim, rel4u_net_sim_stats_t* out_stats);

/**
 * @brief Stop proxy worker thread.
 * @param sim Proxy handle.
 */
void rel4u_net_sim_stop(rel4u_net_sim_t* sim);

/**
 * @brief Stop and destroy the proxy instance.
 * @param sim Proxy handle.
 */
void rel4u_net_sim_destroy(rel4u_net_sim_t* sim);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_SIM_H */
