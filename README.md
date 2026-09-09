# rel4u — Reliable Messaging for UDP

[![Standard: C11](https://img.shields.io/badge/standard-C11-blue.svg)](#)
[![Build System: CMake](https://img.shields.io/badge/build-CMake%20%3E%3D%203.16-brightgreen.svg)](#)
[![Platform: Linux | Windows | BSD](https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20BSD-lightgrey.svg)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](#)

`rel4u` is a lightweight, high-performance, production-grade **C11 library** providing reliable, ordered, and sequenced messaging capabilities over raw UDP sockets.

Designed for latency-critical and high-throughput systems—including game engines, real-time telemetry, robotics, and distributed control systems—`rel4u` delivers the reliability semantics of TCP with the low latency and packet control of UDP.

---

## Key Features & Design Principles

- **Configurable Memory Allocation (Fixed or Dynamic):**
  - *Fixed Mode (Default):* Memory is allocated only during instance initialization. Zero dynamic allocations (`malloc`/`free`) occur during packet processing, sending, receiving, or retransmitting.
  - *Dynamic Mode:* Messages are dynamically allocated with `malloc`/`free` per packet, strictly bounded by a configurable `max_send_memory_bytes` and `max_recv_memory_bytes` ceiling.
- **Selectable Queue Full Policies (`DROP_NEW` or `DROP_LAST`):** When buffers reach capacity or byte limits, choose between rejecting incoming packets (`REL4U_QUEUE_FULL_DROP_NEW`) or evicting the oldest queued packets (`REL4U_QUEUE_FULL_DROP_LAST`).
- **Thread-Safe MPMC Queues:** Inter-thread communication between application threads (multiple writers/readers) and the network worker thread uses bounded, thread-safe Multi-Producer Multi-Consumer (MPMC) queues.
- **4 Flexible Delivery Modes:** Choose per-packet guarantees from unreliable fire-and-forget to strictly ordered reliable delivery.
- **Selective Acknowledgment (SACK):** 32-bit SACK bitmasks enable efficient selective retransmission without duplicate bandwidth waste.
- **Adaptive RTO Engine:** Real-time Round-Trip Time tracking and retransmission timeouts via the Jacobson-Karels algorithm.
- **Cross-Platform & Zero Dependencies:** Pure C11 (`-std=c11`) with native platform backends for Linux/POSIX, Windows (WinSock2), and BSD/macOS.

---

## Delivery Modes

| Mode Enum | Reliability | Ordering | Retransmission | Description |
|---|---|---|---|---|
| `REL4U_MODE_UNRELIABLE_UNORDERED` | None | None | No | Fire-and-forget raw UDP; delivered immediately. |
| `REL4U_MODE_UNRELIABLE_ORDERED` | None | Sequenced | No | Drops stale packets; delivers only newer state updates. |
| `REL4U_MODE_RELIABLE_UNORDERED` | Guaranteed | None | Yes | Guaranteed arrival; delivered on first receipt. |
| `REL4U_MODE_RELIABLE_ORDERED` | Guaranteed | Strict Sequential | Yes | Contiguous sequential delivery with reassembly sliding window. |

---

## Quickstart Examples

### Minimal Server (Echo Server)

```c
#include "rel4u.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    rel4u_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.bind_address        = "0.0.0.0";
    config.bind_port           = 9000;
    config.max_clients         = 64;
    config.full_policy         = REL4U_MAX_CLIENTS_DROP_OLD;
    config.mtu                 = REL4U_DEFAULT_MTU;
    config.send_queue_capacity = 512;
    config.recv_queue_capacity = 512;

    rel4u_server_t* server = rel4u_server_create(&config);
    if (!server || rel4u_server_start(server) != REL4U_OK) {
        fprintf(stderr, "Failed to start rel4u server\n");
        return 1;
    }

    printf("Server listening on port 9000...\n");

    char buffer[REL4U_DEFAULT_MTU];
    size_t len = 0;
    uint32_t client_id = 0;

    while (1) {
        // Wait up to 100ms for incoming packets
        if (rel4u_server_recv(server, &client_id, buffer, sizeof(buffer), &len, 100) == REL4U_OK) {
            printf("Received %zu bytes from client %u. Echoing back...\n", len, client_id);
            rel4u_server_send(server, client_id, REL4U_MODE_RELIABLE_ORDERED, buffer, len);
        }
    }

    rel4u_server_destroy(server);
    return 0;
}
```

### Minimal Client

```c
#include "rel4u.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    rel4u_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.server_address      = "127.0.0.1";
    config.server_port         = 9000;
    config.mtu                 = REL4U_DEFAULT_MTU;
    config.send_queue_capacity = 256;
    config.recv_queue_capacity = 256;
    config.connect_timeout_ms  = 3000;

    rel4u_client_t* client = rel4u_client_create(&config);
    if (!client || rel4u_client_connect(client) != REL4U_OK) {
        fprintf(stderr, "Failed to connect to server\n");
        return 1;
    }

    const char* msg = "Hello from rel4u client!";
    rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, msg, strlen(msg));

    char buffer[REL4U_DEFAULT_MTU];
    size_t len = 0;
    if (rel4u_client_recv(client, buffer, sizeof(buffer), &len, 1000) == REL4U_OK) {
        buffer[len] = '\0';
        printf("Server replied: %s\n", buffer);
    }

    rel4u_client_disconnect(client);
    rel4u_client_destroy(client);
    return 0;
}
```

---

## Building & Testing

### Requirements
- C11 compatible compiler (`gcc`, `clang`, `MSVC`)
- CMake $\ge 3.16$
- [Ninja](https://ninja-build.org/) build tool

### Build Steps

A single build invocation generates all artifacts (static library `librel4u.a`, shared library `librel4u.so`, unit tests, integration tests, and example binaries).

```bash
# Clone the repository
git clone https://github.com/tbeauchant/rel4u.git
cd rel4u

# Configure with Ninja (Debug by default, or pass -DCMAKE_BUILD_TYPE=Release)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build

# Run unit and integration tests
ctest --test-dir build --output-on-failure
```

### Running the Included Examples

In terminal 1:
```bash
./build/server_echo 9000
```

In terminal 2:
```bash
./build/client_bench 127.0.0.1 9000 5000
```

---

## CMake Integration

### Option A: `FetchContent` (Recommended)

```cmake
include(FetchContent)
FetchContent_Declare(
    rel4u
    GIT_REPOSITORY https://github.com/tbeauchant/rel4u.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(rel4u)

# Link against static or shared library alias:
target_link_libraries(my_application PRIVATE rel4u::static)   # or rel4u::shared
```

### Option B: `add_subdirectory`

```cmake
add_subdirectory(third_party/rel4u)
target_link_libraries(my_application PRIVATE rel4u::static)   # or rel4u::shared
```

---

## Documentation Index

Explore the comprehensive technical guides in the [`docs/`](docs/) directory:

- [**System Architecture**](docs/architecture.md): Topology, actor-style threading model, and thread-safe bounded queue concurrency design.
- [**Wire Protocol Specification**](docs/protocol.md): 24-byte packet header format, 3-way handshake, state machines, SACK engine, and RTO math.
- [**Public API Reference**](docs/api-reference.md): Detailed reference for functions, data types, error codes, and configuration options.
- [**Internal Subsystems**](docs/internals.md): Bounded MPMC queue, sliding window reassembly, flat session management, and platform abstraction layer.

---

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.
