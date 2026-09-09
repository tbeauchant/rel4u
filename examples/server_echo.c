#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "rel4u.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#if defined(_WIN32)
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#include <time.h>
static inline void sleep_ms(unsigned int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

static volatile int g_keep_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_keep_running = 0;
}

int main(int argc, char** argv) {
    uint16_t port = 9000;
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
    }

    signal(SIGINT, sigint_handler);

    printf("[SERVER] Starting rel4u echo server on port %u...\n", (unsigned int)port);

    rel4u_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.bind_address = "0.0.0.0";
    config.bind_port = port;
    config.max_clients = 64;
    config.full_policy = REL4U_MAX_CLIENTS_DROP_OLD;
    config.mtu = REL4U_DEFAULT_MTU;
    config.send_queue_capacity = 512;
    config.recv_queue_capacity = 512;

    rel4u_server_t* server = rel4u_server_create(&config);
    if (!server) {
        fprintf(stderr, "[SERVER ERROR] Failed to create server instance.\n");
        return 1;
    }

    if (rel4u_server_start(server) != REL4U_OK) {
        fprintf(stderr, "[SERVER ERROR] Failed to start server on port %u.\n", (unsigned int)port);
        rel4u_server_destroy(server);
        return 1;
    }

    printf("[SERVER] Listening for connections. Press Ctrl+C to exit.\n");

    char buffer[REL4U_DEFAULT_MTU];
    size_t len = 0;
    uint32_t client_id = 0;
    uint64_t echo_count = 0;

    while (g_keep_running) {
        int res = rel4u_server_recv(server, &client_id, buffer, sizeof(buffer), &len, 100);
        if (res == REL4U_OK) {
            echo_count++;
            // Echo back to sender (retry if send queue temporarily full)
            while (g_keep_running && rel4u_server_send(server, client_id, REL4U_MODE_RELIABLE_ORDERED, buffer, len) != REL4U_OK) {
                sleep_ms(1);
            }

            if (echo_count % 500 == 0) {
                rel4u_stats_t stats;
                rel4u_server_get_stats(server, &stats);
                printf("[SERVER STATS] Echoed %llu msgs | Active clients: %u | Pkts sent: %llu, recv: %llu\n",
                       (unsigned long long)echo_count, stats.active_connections,
                       (unsigned long long)stats.packets_sent, (unsigned long long)stats.packets_recv);
            }
        }
    }

    printf("\n[SERVER] Stopping server...\n");
    rel4u_server_destroy(server);
    printf("[SERVER] Shutdown complete.\n");
    return 0;
}
