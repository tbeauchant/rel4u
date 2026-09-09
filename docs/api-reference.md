# rel4u Public C11 API Reference

This document provides a comprehensive reference for all types, structures, enums, constants, and functions exposed by the public header [`include/rel4u.h`](include/rel4u.h).

---

## 1. Constants & Macros

```c
#define REL4U_DEFAULT_MTU           1400
#define REL4U_HEADER_SIZE           24
#define REL4U_MAX_PAYLOAD(mtu)      ((size_t)((mtu) > REL4U_HEADER_SIZE ? ((mtu) - REL4U_HEADER_SIZE) : 0))
#define REL4U_INVALID_CLIENT_ID     0xFFFFFFFFU
```

- `REL4U_DEFAULT_MTU`: Standard safe Ethernet UDP payload limit (1400 bytes, avoiding IP fragmentation).
- `REL4U_HEADER_SIZE`: Size of the fixed binary protocol header (24 bytes).
- `REL4U_MAX_PAYLOAD(mtu)`: Macro returning the maximum allowable application data payload for a given MTU.
- `REL4U_INVALID_CLIENT_ID`: Sentinel value indicating an unassigned or invalid client session identifier.

---

## 2. Return Codes & Error Handling

All functions returning `int` yield `REL4U_OK` (`0`) on success, or a negative error code on failure:

| Error Code | Value | Description | Recommended Action |
|---|---|---|---|
| `REL4U_OK` | `0` | Operation succeeded. | None. |
| `REL4U_ERR_INVALID_PARAM` | `-1` | Null pointer or invalid argument passed. | Check pointer validity and non-zero parameters. |
| `REL4U_ERR_NO_MEMORY` | `-2` | Failed to allocate memory during initialization. | Reduce queue capacities or window sizes. |
| `REL4U_ERR_SOCKET` | `-3` | Socket creation, binding, or I/O failure. | Check port availability and OS permissions. |
| `REL4U_ERR_QUEUE_FULL` | `-4` | Outbound MPMC queue is saturated. | Back off or increase `send_queue_capacity`. |
| `REL4U_ERR_QUEUE_EMPTY` | `-5` | Inbound MPMC queue is empty (non-blocking recv). | Poll again or use blocking timeout. |
| `REL4U_ERR_TIMEOUT` | `-6` | Receive timed out without available messages. | Continue event loop or handle timeout. |
| `REL4U_ERR_NOT_CONNECTED` | `-7` | Endpoint is not currently in `CONNECTED` state. | Reconnect or await handshake completion. |
| `REL4U_ERR_MSG_TOO_LARGE` | `-8` | Message size exceeds `REL4U_MAX_PAYLOAD(mtu)`. | Chunk message at application level. |
| `REL4U_ERR_SERVER_FULL` | `-9` | Max concurrent client sessions reached. | Reject or configure `DROP_OLD` policy. |
| `REL4U_ERR_CLIENT_NOT_FOUND`| `-10`| Specified `client_id` is inactive or invalid. | Verify `client_id` from inbound packet. |
| `REL4U_ERR_SYSTEM` | `-11` | OS thread or synchronization primitive failure. | Check system resource limits. |

---

## 3. Enumerations

### `rel4u_delivery_mode_t`

Specifies the delivery guarantee for an individual message:

```c
typedef enum rel4u_delivery_mode {
    REL4U_MODE_UNRELIABLE_UNORDERED = 0, /**< Pure UDP fire & forget, no order guarantee. */
    REL4U_MODE_UNRELIABLE_ORDERED   = 1, /**< Unreliable but drops stale packets (newest only). */
    REL4U_MODE_RELIABLE_UNORDERED   = 2, /**< Guaranteed delivery, delivered immediately on arrival. */
    REL4U_MODE_RELIABLE_ORDERED     = 3  /**< Guaranteed delivery in strict sequential order. */
} rel4u_delivery_mode_t;
```

### `rel4u_max_clients_policy_t`

Controls server behavior when connection request is received while all client slots are occupied:

```c
typedef enum rel4u_max_clients_policy {
    REL4U_MAX_CLIENTS_REJECT   = 0, /**< Send REJECT packet to incoming client. */
    REL4U_MAX_CLIENTS_DROP_OLD = 1  /**< Evict least recently active (LRU) client to admit new client. */
} rel4u_max_clients_policy_t;
```

### `rel4u_conn_state_t`

State of a client connection:

```c
typedef enum rel4u_conn_state {
    REL4U_STATE_DISCONNECTED  = 0,
    REL4U_STATE_CONNECTING    = 1,
    REL4U_STATE_CONNECTED     = 2,
    REL4U_STATE_DISCONNECTING = 3
} rel4u_conn_state_t;
```

### `rel4u_memory_mode_t`

Memory allocation strategy for internal application queues:

```c
typedef enum rel4u_memory_mode {
    REL4U_MEM_FIXED   = 0, /**< Pre-allocated ring buffers at init, zero dynamic allocations during runtime. */
    REL4U_MEM_DYNAMIC = 1  /**< Dynamic memory allocation with malloc/free bounded by max byte limits. */
} rel4u_memory_mode_t;
```

### `rel4u_queue_full_policy_t`

Behavior when queues or buffers reach capacity or memory limits:

```c
typedef enum rel4u_queue_full_policy {
    REL4U_QUEUE_FULL_DROP_NEW  = 0, /**< Reject incoming message when buffer is full. */
    REL4U_QUEUE_FULL_DROP_LAST = 1  /**< Evict oldest queued message (head) to admit new message. */
} rel4u_queue_full_policy_t;
```

---

## 4. Configuration Structures

### `rel4u_server_config_t`

```c
typedef struct rel4u_server_config {
    const char*                bind_address;          /**< Local IP (e.g. "0.0.0.0"). */
    uint16_t                   bind_port;             /**< UDP port to listen on. */
    uint32_t                   max_clients;           /**< Max concurrent clients (e.g. 64). */
    rel4u_max_clients_policy_t full_policy;           /**< REJECT or DROP_OLD. */
    uint16_t                   mtu;                   /**< Network MTU (default: 1400). */
    uint32_t                   send_queue_capacity;   /**< Outbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   recv_queue_capacity;   /**< Inbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   window_size;           /**< Sliding window packet slots (e.g. 64). */
    uint32_t                   heartbeat_interval_ms; /**< Ping interval in ms (e.g. 1000). */
    uint32_t                   inactivity_timeout_ms; /**< Inactivity timeout in ms (e.g. 5000). */
    rel4u_memory_mode_t        memory_mode;           /**< REL4U_MEM_FIXED (default) or REL4U_MEM_DYNAMIC. */
    rel4u_queue_full_policy_t  queue_full_policy;     /**< REL4U_QUEUE_FULL_DROP_NEW (default) or REL4U_QUEUE_FULL_DROP_LAST. */
    size_t                     max_send_memory_bytes; /**< Dynamic mode: max bytes for send queue (0 for default). */
    size_t                     max_recv_memory_bytes; /**< Dynamic mode: max bytes for recv queue (0 for default). */
} rel4u_server_config_t;
```

### `rel4u_client_config_t`

```c
typedef struct rel4u_client_config {
    const char*                server_address;        /**< Server hostname / IP (e.g. "127.0.0.1"). */
    uint16_t                   server_port;           /**< Server UDP port. */
    const char*                bind_address;          /**< Optional local bind IP, NULL for default. */
    uint16_t                   bind_port;             /**< Optional local port, 0 for OS assigned. */
    uint16_t                   mtu;                   /**< Network MTU (default: 1400). */
    uint32_t                   send_queue_capacity;   /**< Outbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   recv_queue_capacity;   /**< Inbound MPMC queue capacity (power of 2, fixed mode). */
    uint32_t                   window_size;           /**< Sliding window packet slots (e.g. 64). */
    uint32_t                   heartbeat_interval_ms; /**< Ping interval in ms (e.g. 1000). */
    uint32_t                   inactivity_timeout_ms; /**< Inactivity timeout in ms (e.g. 5000). */
    uint32_t                   connect_timeout_ms;    /**< Handshake timeout in ms (e.g. 3000). */
    bool                       disable_auto_reconnect;/**< If true, disables auto-reconnection on timeout/reset (default false). */
    uint32_t                   reconnect_interval_ms; /**< Interval between auto-reconnect retries in ms (default 1000). */
    rel4u_memory_mode_t        memory_mode;           /**< REL4U_MEM_FIXED (default) or REL4U_MEM_DYNAMIC. */
    rel4u_queue_full_policy_t  queue_full_policy;     /**< REL4U_QUEUE_FULL_DROP_NEW (default) or REL4U_QUEUE_FULL_DROP_LAST. */
    size_t                     max_send_memory_bytes; /**< Dynamic mode: max bytes for send queue (0 for default). */
    size_t                     max_recv_memory_bytes; /**< Dynamic mode: max bytes for recv queue (0 for default). */
} rel4u_client_config_t;
```

### `rel4u_stats_t`

```c
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
```

---

## 5. Server API Functions

### Lifecycle

#### `rel4u_server_create`
```c
rel4u_server_t* rel4u_server_create(const rel4u_server_config_t* config);
```
Pre-allocates all memory, queues, and session structures. Returns `NULL` on failure.

#### `rel4u_server_start`
```c
int rel4u_server_start(rel4u_server_t* server);
```
Binds the UDP socket and starts the background worker thread.

#### `rel4u_server_stop`
```c
int rel4u_server_stop(rel4u_server_t* server);
```
Stops the worker thread and closes the socket without freeing internal memory.

#### `rel4u_server_destroy`
```c
void rel4u_server_destroy(rel4u_server_t* server);
```
Stops the server if running, frees all pre-allocated memory buffers, and releases the handle.

---

### Data Transfer & Control

#### `rel4u_server_send`
```c
int rel4u_server_send(rel4u_server_t* server, uint32_t client_id,
                      rel4u_delivery_mode_t mode,
                      const void* data, size_t len);
```
Enqueues a message for transmission to a specific connected client. Thread-safe for multiple concurrent callers.

#### `rel4u_server_recv`
```c
int rel4u_server_recv(rel4u_server_t* server, uint32_t* out_client_id,
                      void* buffer, size_t buffer_size,
                      size_t* out_len, int32_t timeout_ms);
```
Pulls an incoming message from the server receive queue.
- `timeout_ms == 0`: Non-blocking (returns `REL4U_ERR_QUEUE_EMPTY` immediately if no packet is ready).
- `timeout_ms < 0`: Infinite blocking.
- `timeout_ms > 0`: Blocks up to `timeout_ms` milliseconds before returning `REL4U_ERR_TIMEOUT`.

#### `rel4u_server_disconnect_client`
```c
int rel4u_server_disconnect_client(rel4u_server_t* server, uint32_t client_id);
```
Gracefully kicks/disconnects a specific client.

#### `rel4u_server_get_stats` / `rel4u_server_get_client_stats`
```c
int rel4u_server_get_stats(rel4u_server_t* server, rel4u_stats_t* out_stats);
int rel4u_server_get_client_stats(rel4u_server_t* server, uint32_t client_id, rel4u_stats_t* out_stats);
```
Fetches aggregate or per-client performance and network metrics.

---

## 6. Client API Functions

### Lifecycle

#### `rel4u_client_create`
```c
rel4u_client_t* rel4u_client_create(const rel4u_client_config_t* config);
```
Allocates queues and sliding window buffers.

#### `rel4u_client_connect`
```c
int rel4u_client_connect(rel4u_client_t* client);
```
Spawns the background worker and completes the 3-way handshake with the target server. Blocks until connected or timeout.

#### `rel4u_client_disconnect`
```c
int rel4u_client_disconnect(rel4u_client_t* client);
```
Sends a `DISCONNECT` packet and gracefully closes the connection.

#### `rel4u_client_destroy`
```c
void rel4u_client_destroy(rel4u_client_t* client);
```
Disconnects if active, stops the worker thread, and frees all memory.

---

### Data Transfer & Monitoring

#### `rel4u_client_send`
```c
int rel4u_client_send(rel4u_client_t* client,
                      rel4u_delivery_mode_t mode,
                      const void* data, size_t len);
```
Thread-safe enqueue to the client outbound queue.

#### `rel4u_client_recv`
```c
int rel4u_client_recv(rel4u_client_t* client,
                      void* buffer, size_t buffer_size,
                      size_t* out_len, int32_t timeout_ms);
```
Thread-safe retrieval of incoming messages with configurable timeout.

#### `rel4u_client_get_state`
```c
rel4u_conn_state_t rel4u_client_get_state(rel4u_client_t* client);
```
Returns the current `rel4u_conn_state_t` enum value.

#### `rel4u_client_get_stats`
```c
int rel4u_client_get_stats(rel4u_client_t* client, rel4u_stats_t* out_stats);
```
Fetches live telemetry (RTT, RTO, packet loss, retransmissions, throughput).
