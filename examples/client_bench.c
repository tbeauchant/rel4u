#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "rel4u.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char** argv) {
    const char* server_ip = "127.0.0.1";
    uint16_t port = 9000;
    int num_messages = 1000;

    if (argc > 1) server_ip = argv[1];
    if (argc > 2) port = (uint16_t)atoi(argv[2]);
    if (argc > 3) num_messages = atoi(argv[3]);

    printf("[CLIENT BENCH] Connecting to %s:%u (Sending %d messages)...\n", server_ip, (unsigned int)port, num_messages);

    rel4u_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.server_address = server_ip;
    config.server_port = port;
    config.mtu = REL4U_DEFAULT_MTU;
    config.send_queue_capacity = 1024;
    config.recv_queue_capacity = 1024;
    config.window_size = 128;
    config.connect_timeout_ms = 3000;

    rel4u_client_t* client = rel4u_client_create(&config);
    if (!client) {
        fprintf(stderr, "[CLIENT ERROR] Failed to create client.\n");
        return 1;
    }

    if (rel4u_client_connect(client) != REL4U_OK) {
        fprintf(stderr, "[CLIENT ERROR] Failed to connect to server.\n");
        rel4u_client_destroy(client);
        return 1;
    }

    printf("[CLIENT] Connected successfully!\n");

    char send_buf[128];
    char recv_buf[REL4U_DEFAULT_MTU];
    size_t recv_len = 0;
    int received_count = 0;

    for (int i = 1; i <= num_messages; i++) {
        snprintf(send_buf, sizeof(send_buf), "BENCHMARK_MSG_%06d", i);
        while (rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, send_buf, strlen(send_buf)) != REL4U_OK) {
            sleep_ms(1);
        }

        // Drain any ready echoes
        while (rel4u_client_recv(client, recv_buf, sizeof(recv_buf), &recv_len, 0) == REL4U_OK) {
            received_count++;
        }
    }

    // Wait for remaining echoes
    while (received_count < num_messages) {
        if (rel4u_client_recv(client, recv_buf, sizeof(recv_buf), &recv_len, 1000) == REL4U_OK) {
            received_count++;
        } else {
            break;
        }
    }

    printf("[CLIENT BENCH COMPLETE] Sent: %d, Received Echoes: %d\n", num_messages, received_count);

    rel4u_stats_t stats;
    rel4u_client_get_stats(client, &stats);
    printf("[CLIENT STATS] RTT: %u ms | RTO: %u ms | Pkts Sent: %llu | Pkts Recv: %llu | Lost: %llu | Retrans: %llu\n",
           stats.current_rtt_ms, stats.current_rto_ms,
           (unsigned long long)stats.packets_sent, (unsigned long long)stats.packets_recv,
           (unsigned long long)stats.packets_lost, (unsigned long long)stats.packets_retransmitted);

    rel4u_client_destroy(client);
    return (received_count == num_messages) ? 0 : 1;
}
