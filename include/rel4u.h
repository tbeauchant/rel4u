/**
 * @file rel4u.h
 * @brief Reliable Messaging for UDP (rel4u) public C11 API.
 */

#ifndef REL4U_H
#define REL4U_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- API Export / Visibility Macro --- */
#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(REL4U_BUILDING_DLL)
    #define REL4U_API __declspec(dllexport)
  #elif defined(REL4U_STATIC)
    #define REL4U_API
  #else
    #define REL4U_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define REL4U_API __attribute__((visibility("default")))
#else
  #define REL4U_API
#endif

/* --- Return / Error Codes --- */
#define REL4U_OK                     0
#define REL4U_ERR_INVALID_PARAM     -1
#define REL4U_ERR_NO_MEMORY         -2
#define REL4U_ERR_SOCKET            -3
#define REL4U_ERR_QUEUE_FULL        -4
#define REL4U_ERR_QUEUE_EMPTY       -5
#define REL4U_ERR_TIMEOUT           -6
#define REL4U_ERR_NOT_CONNECTED     -7
#define REL4U_ERR_MSG_TOO_LARGE     -8
#define REL4U_ERR_SERVER_FULL       -9
#define REL4U_ERR_CLIENT_NOT_FOUND  -10
#define REL4U_ERR_SYSTEM            -11

/* --- Default Constants --- */
#define REL4U_DEFAULT_MTU           1400
#define REL4U_HEADER_SIZE           24
#define REL4U_MAX_PAYLOAD(mtu)      ((size_t)((mtu) > REL4U_HEADER_SIZE ? ((mtu) - REL4U_HEADER_SIZE) : 0))
#define REL4U_INVALID_CLIENT_ID     0xFFFFFFFFU

/* --- Enums --- */

/**
 * @brief Delivery guarantee modes for messages.
 */
typedef enum rel4u_delivery_mode {
    REL4U_MODE_UNRELIABLE_UNORDERED = 0, /**< Pure UDP (fire & forget, no order guarantee). */
    REL4U_MODE_UNRELIABLE_ORDERED   = 1, /**< Unreliable but drops stale packets (newest only). */
    REL4U_MODE_RELIABLE_UNORDERED   = 2, /**< Guaranteed delivery, delivered immediately on arrival. */
    REL4U_MODE_RELIABLE_ORDERED     = 3  /**< Guaranteed delivery in strict sequential order. */
} rel4u_delivery_mode_t;

/**
 * @brief Eviction / admission policy when server reaches max_clients.
 */
typedef enum rel4u_max_clients_policy {
    REL4U_MAX_CLIENTS_REJECT   = 0, /**< Reject incoming new client with rejection packet. */
    REL4U_MAX_CLIENTS_DROP_OLD = 1  /**< Evict least recently active (LRU) client to admit new client. */
} rel4u_max_clients_policy_t;

/**
 * @brief Memory allocation strategy for internal queues/buffers.
 */
typedef enum rel4u_memory_mode {
    REL4U_MEM_FIXED   = 0, /**< Pre-allocated ring buffers at init, zero dynamic allocations during runtime. */
    REL4U_MEM_DYNAMIC = 1  /**< Dynamic memory allocation with malloc/free bounded by max byte limits. */
} rel4u_memory_mode_t;

/**
 * @brief Behavior when queues or buffers reach capacity / max memory limits.
 */
typedef enum rel4u_queue_full_policy {
    REL4U_QUEUE_FULL_DROP_NEW  = 0, /**< Reject incoming message when buffer is full. */
    REL4U_QUEUE_FULL_DROP_LAST = 1  /**< Evict oldest queued message (head) to admit new message. */
} rel4u_queue_full_policy_t;

/**
 * @brief Connection state of client or client slot.
 */
typedef enum rel4u_conn_state {
    REL4U_STATE_DISCONNECTED = 0,
    REL4U_STATE_CONNECTING   = 1,
    REL4U_STATE_CONNECTED    = 2,
    REL4U_STATE_DISCONNECTING= 3
} rel4u_conn_state_t;

/* --- Configuration Structs --- */

/**
 * @brief Server configuration options.
 */
typedef struct rel4u_server_config {
    const char*                bind_address;             /**< Local address to bind (e.g. "0.0.0.0"). */
    uint16_t                   bind_port;                /**< UDP port to listen on. */
    uint32_t                   max_clients;              /**< Maximum concurrent clients (e.g. 64). */
    rel4u_max_clients_policy_t full_policy;              /**< REJECT or DROP_OLD. */
    uint16_t                   mtu;                      /**< Network MTU (default 1400). */
    uint32_t                   send_queue_capacity;      /**< Outbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   recv_queue_capacity;      /**< Inbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   window_size;              /**< Sliding window packet slots (e.g. 64). */
    uint32_t                   heartbeat_interval_ms;    /**< Ping interval in ms (e.g. 1000). */
    uint32_t                   inactivity_timeout_ms;    /**< Inactivity timeout in ms (e.g. 5000). */
    rel4u_memory_mode_t        memory_mode;              /**< REL4U_MEM_FIXED (default) or REL4U_MEM_DYNAMIC. */
    rel4u_queue_full_policy_t  queue_full_policy;        /**< REL4U_QUEUE_FULL_DROP_NEW (default) or REL4U_QUEUE_FULL_DROP_LAST. */
    size_t                     max_send_memory_bytes;    /**< Dynamic mode: max bytes for send queue (0 for default). */
    size_t                     max_recv_memory_bytes;    /**< Dynamic mode: max bytes for recv queue (0 for default). */
} rel4u_server_config_t;

/**
 * @brief Client configuration options.
 */
typedef struct rel4u_client_config {
    const char*                server_address;           /**< Server hostname / IP (e.g. "127.0.0.1"). */
    uint16_t                   server_port;              /**< Server UDP port. */
    const char*                bind_address;             /**< Optional local bind IP, NULL for default. */
    uint16_t                   bind_port;                /**< Optional local port, 0 for OS assigned. */
    uint16_t                   mtu;                      /**< Network MTU (default 1400). */
    uint32_t                   send_queue_capacity;      /**< Outbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   recv_queue_capacity;      /**< Inbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   window_size;              /**< Sliding window packet slots (e.g. 64). */
    uint32_t                   heartbeat_interval_ms;    /**< Ping interval in ms (e.g. 1000). */
    uint32_t                   inactivity_timeout_ms;    /**< Inactivity timeout in ms (e.g. 5000). */
    uint32_t                   connect_timeout_ms;       /**< Handshake timeout in ms (e.g. 3000). */
    bool                       disable_auto_reconnect;   /**< If true, disables auto-reconnection on timeout/reset (default false). */
    uint32_t                   reconnect_interval_ms;    /**< Interval between auto-reconnect retries in ms (default 1000). */
    rel4u_memory_mode_t        memory_mode;              /**< REL4U_MEM_FIXED (default) or REL4U_MEM_DYNAMIC. */
    rel4u_queue_full_policy_t  queue_full_policy;        /**< REL4U_QUEUE_FULL_DROP_NEW (default) or REL4U_QUEUE_FULL_DROP_LAST. */
    size_t                     max_send_memory_bytes;    /**< Dynamic mode: max bytes for send queue (0 for default). */
    size_t                     max_recv_memory_bytes;    /**< Dynamic mode: max bytes for recv queue (0 for default). */
} rel4u_client_config_t;

/**
 * @brief Endpoint runtime statistics.
 */
typedef struct rel4u_stats {
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t packets_lost;
    uint64_t packets_retransmitted;
    uint64_t packets_dropped_queue_full;
    uint32_t current_rtt_ms;
    uint32_t current_rto_ms;
    uint32_t active_connections;        /**< Server only */
    uint32_t send_queue_occupancy;
    uint32_t recv_queue_occupancy;
    uint64_t send_queue_bytes;
    uint64_t recv_queue_bytes;
} rel4u_stats_t;

/* --- Opaque Types --- */
typedef struct rel4u_server rel4u_server_t;
typedef struct rel4u_client rel4u_client_t;

/* --- Server API --- */

/**
 * @brief Create a server instance. Pre-allocates all queues and slot tables.
 * @param config Pointer to configuration struct.
 * @return Server handle on success, NULL on failure.
 */
REL4U_API rel4u_server_t* rel4u_server_create(const rel4u_server_config_t* config);

/**
 * @brief Bind server UDP socket and start the background worker thread.
 * @param server Server handle.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_server_start(rel4u_server_t* server);

/**
 * @brief Stop background worker thread and close server socket.
 * @param server Server handle.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_server_stop(rel4u_server_t* server);

/**
 * @brief Free all memory and resources associated with the server.
 * @param server Server handle.
 */
REL4U_API void rel4u_server_destroy(rel4u_server_t* server);

/**
 * @brief Thread-safe send of a message to a specific connected client.
 * @param server Server handle.
 * @param client_id Target client ID.
 * @param mode Delivery guarantee mode.
 * @param data Pointer to payload data.
 * @param len Length of payload in bytes (must be <= max payload).
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_server_send(rel4u_server_t* server, uint32_t client_id,
                      rel4u_delivery_mode_t mode,
                      const void* data, size_t len);

/**
 * @brief Thread-safe receive of an incoming message from any connected client.
 * @param server Server handle.
 * @param out_client_id Pointer to store the sender's client ID (can be NULL).
 * @param buffer Buffer to receive message payload.
 * @param buffer_size Size of destination buffer.
 * @param out_len Pointer to store actual message length received (can be NULL).
 * @param timeout_ms Timeout in ms: 0 for non-blocking, < 0 for infinite blocking, > 0 for timed blocking.
 * @return REL4U_OK (0) on success, REL4U_ERR_TIMEOUT if timed out, or error code.
 */
REL4U_API int rel4u_server_recv(rel4u_server_t* server, uint32_t* out_client_id,
                      void* buffer, size_t buffer_size,
                      size_t* out_len, int32_t timeout_ms);

/**
 * @brief Disconnect a specific client from the server.
 * @param server Server handle.
 * @param client_id Target client ID.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_server_disconnect_client(rel4u_server_t* server, uint32_t client_id);

/**
 * @brief Retrieve overall server statistics.
 * @param server Server handle.
 * @param out_stats Pointer to destination stats struct.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_server_get_stats(rel4u_server_t* server, rel4u_stats_t* out_stats);

/**
 * @brief Retrieve statistics for a specific client.
 * @param server Server handle.
 * @param client_id Target client ID.
 * @param out_stats Pointer to destination stats struct.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_server_get_client_stats(rel4u_server_t* server, uint32_t client_id, rel4u_stats_t* out_stats);

/* --- Client API --- */

/**
 * @brief Create a client instance. Pre-allocates send/receive queues and window.
 * @param config Pointer to client configuration struct.
 * @return Client handle on success, NULL on failure.
 */
REL4U_API rel4u_client_t* rel4u_client_create(const rel4u_client_config_t* config);

/**
 * @brief Connect to the configured server (starts background worker and completes handshake).
 * @param client Client handle.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_client_connect(rel4u_client_t* client);

/**
 * @brief Gracefully disconnect from server.
 * @param client Client handle.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_client_disconnect(rel4u_client_t* client);

/**
 * @brief Free all memory and resources associated with the client.
 * @param client Client handle.
 */
REL4U_API void rel4u_client_destroy(rel4u_client_t* client);

/**
 * @brief Thread-safe send of a message to the connected server.
 * @param client Client handle.
 * @param mode Delivery guarantee mode.
 * @param data Pointer to payload data.
 * @param len Length of payload in bytes (must be <= max payload).
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_client_send(rel4u_client_t* client,
                      rel4u_delivery_mode_t mode,
                      const void* data, size_t len);

/**
 * @brief Thread-safe receive of an incoming message from the server.
 * @param client Client handle.
 * @param buffer Buffer to receive message payload.
 * @param buffer_size Size of destination buffer.
 * @param out_len Pointer to store actual message length received (can be NULL).
 * @param timeout_ms Timeout in ms: 0 for non-blocking, < 0 for infinite blocking, > 0 for timed blocking.
 * @return REL4U_OK (0) on success, REL4U_ERR_TIMEOUT if timed out, or error code.
 */
REL4U_API int rel4u_client_recv(rel4u_client_t* client,
                      void* buffer, size_t buffer_size,
                      size_t* out_len, int32_t timeout_ms);

/**
 * @brief Get current client connection state.
 * @param client Client handle.
 * @return Current connection state.
 */
REL4U_API rel4u_conn_state_t rel4u_client_get_state(rel4u_client_t* client);

/**
 * @brief Retrieve client statistics.
 * @param client Client handle.
 * @param out_stats Pointer to destination stats struct.
 * @return REL4U_OK (0) on success, or error code.
 */
REL4U_API int rel4u_client_get_stats(rel4u_client_t* client, rel4u_stats_t* out_stats);

#ifdef __cplusplus
}
#endif

#endif /* REL4U_H */
