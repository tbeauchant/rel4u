#include "../framework/test_framework.h"
#include "../../include/rel4u.h"
#include "../../src/platform/rel4u_time.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool test_client_server_connect_disconnect(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19101;
    s_cfg.max_clients = 16;
    s_cfg.full_policy = REL4U_MAX_CLIENTS_REJECT;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19101;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);
    ASSERT_EQ(rel4u_client_get_state(client), REL4U_STATE_CONNECTED);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

static bool test_all_delivery_modes(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19102;
    s_cfg.max_clients = 4;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19102;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    // 1. Send Unreliable Unordered
    const char* msg_unrel = "unreliable_unordered_msg";
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_UNRELIABLE_UNORDERED, msg_unrel, strlen(msg_unrel)), REL4U_OK);

    char recv_buf[256];
    size_t recv_len = 0;
    uint32_t from_client = 0;
    ASSERT_EQ(rel4u_server_recv(server, &from_client, recv_buf, sizeof(recv_buf), &recv_len, 500), REL4U_OK);
    recv_buf[recv_len] = '\0';
    ASSERT_STR_EQ(recv_buf, msg_unrel);

    // 2. Send 50 Reliable Ordered messages Client -> Server
    for (int i = 0; i < 50; i++) {
        char send_str[64];
        snprintf(send_str, sizeof(send_str), "rel_ordered_%03d", i);
        ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, send_str, strlen(send_str)), REL4U_OK);
    }

    for (int i = 0; i < 50; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "rel_ordered_%03d", i);

        ASSERT_EQ(rel4u_server_recv(server, &from_client, recv_buf, sizeof(recv_buf), &recv_len, 1000), REL4U_OK);
        recv_buf[recv_len] = '\0';
        ASSERT_STR_EQ(recv_buf, expected);
    }

    // 3. Send 50 Reliable Ordered messages Server -> Client
    for (int i = 0; i < 50; i++) {
        char send_str[64];
        snprintf(send_str, sizeof(send_str), "srv_to_cli_%03d", i);
        ASSERT_EQ(rel4u_server_send(server, from_client, REL4U_MODE_RELIABLE_ORDERED, send_str, strlen(send_str)), REL4U_OK);
    }

    for (int i = 0; i < 50; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "srv_to_cli_%03d", i);

        ASSERT_EQ(rel4u_client_recv(client, recv_buf, sizeof(recv_buf), &recv_len, 1000), REL4U_OK);
        recv_buf[recv_len] = '\0';
        ASSERT_STR_EQ(recv_buf, expected);
    }

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

static bool test_multi_client_concurrent(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19103;
    s_cfg.max_clients = 8;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    #define NUM_CLIENTS 4
    #define MSGS_PER_CLIENT 20
    rel4u_client_t* clients[NUM_CLIENTS];

    for (int i = 0; i < NUM_CLIENTS; i++) {
        rel4u_client_config_t c_cfg;
        memset(&c_cfg, 0, sizeof(c_cfg));
        c_cfg.server_address = "127.0.0.1";
        c_cfg.server_port = 19103;
        clients[i] = rel4u_client_create(&c_cfg);
        ASSERT_TRUE(clients[i] != NULL);
        ASSERT_EQ(rel4u_client_connect(clients[i]), REL4U_OK);
    }

    // Each client sends 20 messages
    for (int m = 0; m < MSGS_PER_CLIENT; m++) {
        for (int c = 0; c < NUM_CLIENTS; c++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "c%d_m%d", c, m);
            ASSERT_EQ(rel4u_client_send(clients[c], REL4U_MODE_RELIABLE_ORDERED, buf, strlen(buf)), REL4U_OK);
        }
    }

    // Server reads total NUM_CLIENTS * MSGS_PER_CLIENT messages
    int received_count = 0;
    char recv_buf[64];
    size_t recv_len = 0;
    uint32_t from_client = 0;

    for (int i = 0; i < NUM_CLIENTS * MSGS_PER_CLIENT; i++) {
        int res = rel4u_server_recv(server, &from_client, recv_buf, sizeof(recv_buf), &recv_len, 1000);
        ASSERT_EQ(res, REL4U_OK);
        received_count++;
    }

    ASSERT_EQ(received_count, NUM_CLIENTS * MSGS_PER_CLIENT);

    for (int i = 0; i < NUM_CLIENTS; i++) {
        rel4u_client_destroy(clients[i]);
    }
    rel4u_server_destroy(server);
    return true;
}

static bool test_server_max_clients_rejection(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19104;
    s_cfg.max_clients = 2; // Only allow 2 clients
    s_cfg.full_policy = REL4U_MAX_CLIENTS_REJECT;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19104;
    c_cfg.connect_timeout_ms = 500;

    rel4u_client_t* c1 = rel4u_client_create(&c_cfg);
    rel4u_client_t* c2 = rel4u_client_create(&c_cfg);
    rel4u_client_t* c3 = rel4u_client_create(&c_cfg);

    ASSERT_EQ(rel4u_client_connect(c1), REL4U_OK);
    ASSERT_EQ(rel4u_client_connect(c2), REL4U_OK);

    // 3rd client should fail to connect (server full)
    int res = rel4u_client_connect(c3);
    ASSERT_TRUE(res != REL4U_OK);

    rel4u_client_destroy(c1);
    rel4u_client_destroy(c2);
    rel4u_client_destroy(c3);
    rel4u_server_destroy(server);
    return true;
}

static bool test_server_max_clients_lru_eviction(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19105;
    s_cfg.max_clients = 2; // Only 2 slots
    s_cfg.full_policy = REL4U_MAX_CLIENTS_DROP_OLD; // Evict LRU

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19105;
    c_cfg.connect_timeout_ms = 1000;

    rel4u_client_t* c1 = rel4u_client_create(&c_cfg);
    rel4u_client_t* c2 = rel4u_client_create(&c_cfg);
    rel4u_client_t* c3 = rel4u_client_create(&c_cfg);

    ASSERT_EQ(rel4u_client_connect(c1), REL4U_OK);
    rel4u_time_sleep_ms(10);
    ASSERT_EQ(rel4u_client_connect(c2), REL4U_OK);

    // Touch c2 so c1 is the least recently active (LRU)
    const char* msg = "touch";
    rel4u_client_send(c2, REL4U_MODE_UNRELIABLE_UNORDERED, msg, strlen(msg));
    rel4u_time_sleep_ms(50);

    // Now connect c3: c1 should be evicted and c3 admitted!
    ASSERT_EQ(rel4u_client_connect(c3), REL4U_OK);
    ASSERT_EQ(rel4u_client_get_state(c3), REL4U_STATE_CONNECTED);

    rel4u_client_destroy(c1);
    rel4u_client_destroy(c2);
    rel4u_client_destroy(c3);
    rel4u_server_destroy(server);
    return true;
}

static bool test_stats_query(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19106;
    s_cfg.max_clients = 4;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19106;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    const char* msg = "statistics_test_payload";
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, msg, strlen(msg)), REL4U_OK);

    char buf[64];
    size_t len = 0;
    uint32_t from_cid = 0;
    ASSERT_EQ(rel4u_server_recv(server, &from_cid, buf, sizeof(buf), &len, 1000), REL4U_OK);

    rel4u_stats_t c_stats, s_stats, slot_stats;
    ASSERT_EQ(rel4u_client_get_stats(client, &c_stats), REL4U_OK);
    ASSERT_EQ(rel4u_server_get_stats(server, &s_stats), REL4U_OK);
    ASSERT_EQ(rel4u_server_get_client_stats(server, from_cid, &slot_stats), REL4U_OK);

    ASSERT_TRUE(c_stats.packets_sent > 0);
    ASSERT_TRUE(s_stats.packets_recv > 0);
    ASSERT_EQ(s_stats.active_connections, 1);
    ASSERT_TRUE(slot_stats.packets_recv > 0);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

#include "../framework/network_sim.h"

static bool is_heavy_stress_mode(void) {
    const char* env = getenv("REL4U_STRESS_LEVEL");
    return (env && (strcmp(env, "heavy") == 0 || strcmp(env, "HEAVY") == 0 || strcmp(env, "high") == 0));
}

static bool test_stress_reliable_ordered_heavy_loss(void) {
    uint16_t srv_port = 19111;
    uint16_t proxy_port = 19110;

    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = srv_port;
    s_cfg.max_clients = 4;
    s_cfg.send_queue_capacity = 1024;
    s_cfg.recv_queue_capacity = 1024;
    s_cfg.inactivity_timeout_ms = 15000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    // 20% packet drop rate
    rel4u_net_sim_config_t sim_cfg;
    memset(&sim_cfg, 0, sizeof(sim_cfg));
    sim_cfg.drop_rate = 0.20;

    rel4u_net_sim_t* sim = rel4u_net_sim_create(proxy_port, srv_port, &sim_cfg);
    ASSERT_TRUE(sim != NULL);
    ASSERT_EQ(rel4u_net_sim_start(sim), 0);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = proxy_port; // Connect via proxy
    c_cfg.send_queue_capacity = 1024;
    c_cfg.recv_queue_capacity = 1024;
    c_cfg.inactivity_timeout_ms = 15000;
    c_cfg.connect_timeout_ms = 5000;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    int num_messages = is_heavy_stress_mode() ? 500 : 100;

    // Send reliable ordered messages
    for (int i = 0; i < num_messages; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "heavy_loss_msg_%04d", i);
        ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, msg, strlen(msg)), REL4U_OK);
    }

    // Verify 100% in-order reception at server
    char recv_buf[128];
    size_t recv_len = 0;
    uint32_t from_cid = 0;

    for (int i = 0; i < num_messages; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "heavy_loss_msg_%04d", i);

        int res = rel4u_server_recv(server, &from_cid, recv_buf, sizeof(recv_buf), &recv_len, 10000);
        ASSERT_EQ(res, REL4U_OK);
        recv_buf[recv_len] = '\0';
        ASSERT_STR_EQ(recv_buf, expected);
    }

    rel4u_net_sim_stats_t sim_stats;
    rel4u_net_sim_get_stats(sim, &sim_stats);
    ASSERT_TRUE(sim_stats.client_packets_dropped > 0 || sim_stats.server_packets_dropped > 0);

    rel4u_stats_t c_stats;
    ASSERT_EQ(rel4u_client_get_stats(client, &c_stats), REL4U_OK);
    ASSERT_TRUE(c_stats.packets_retransmitted > 0);

    rel4u_client_destroy(client);
    rel4u_net_sim_destroy(sim);
    rel4u_server_destroy(server);
    return true;
}

static bool test_stress_burst_loss_blackout_recovery(void) {
    uint16_t srv_port = 19113;
    uint16_t proxy_port = 19112;

    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = srv_port;
    s_cfg.max_clients = 4;
    s_cfg.send_queue_capacity = 1024;
    s_cfg.recv_queue_capacity = 1024;
    s_cfg.inactivity_timeout_ms = 15000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    // Initial no loss for clean connection handshake
    rel4u_net_sim_config_t sim_cfg;
    memset(&sim_cfg, 0, sizeof(sim_cfg));

    rel4u_net_sim_t* sim = rel4u_net_sim_create(proxy_port, srv_port, &sim_cfg);
    ASSERT_TRUE(sim != NULL);
    ASSERT_EQ(rel4u_net_sim_start(sim), 0);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = proxy_port;
    c_cfg.send_queue_capacity = 1024;
    c_cfg.recv_queue_capacity = 1024;
    c_cfg.inactivity_timeout_ms = 15000;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    // Now inject burst drop: drop 8 consecutive packets every 25 packets
    sim_cfg.burst_drop_count = 8;
    sim_cfg.burst_drop_interval = 25;
    rel4u_net_sim_set_config(sim, &sim_cfg);

    int num_messages = is_heavy_stress_mode() ? 200 : 60;

    for (int i = 0; i < num_messages; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "burst_msg_%04d", i);
        ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, msg, strlen(msg)), REL4U_OK);
    }

    char recv_buf[128];
    size_t recv_len = 0;
    uint32_t from_cid = 0;

    for (int i = 0; i < num_messages; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "burst_msg_%04d", i);

        int res = rel4u_server_recv(server, &from_cid, recv_buf, sizeof(recv_buf), &recv_len, 10000);
        ASSERT_EQ(res, REL4U_OK);
        recv_buf[recv_len] = '\0';
        ASSERT_STR_EQ(recv_buf, expected);
    }

    rel4u_net_sim_stats_t sim_stats;
    rel4u_net_sim_get_stats(sim, &sim_stats);
    ASSERT_TRUE(sim_stats.client_packets_dropped > 0);

    rel4u_client_destroy(client);
    rel4u_net_sim_destroy(sim);
    rel4u_server_destroy(server);
    return true;
}

static bool test_stress_packet_reordering_and_duplication(void) {
    uint16_t srv_port = 19115;
    uint16_t proxy_port = 19114;

    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = srv_port;
    s_cfg.max_clients = 4;
    s_cfg.send_queue_capacity = 1024;
    s_cfg.recv_queue_capacity = 1024;
    s_cfg.inactivity_timeout_ms = 15000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    // Network with 20% reordering, 20% duplication, and 5-15ms jitter
    rel4u_net_sim_config_t sim_cfg;
    memset(&sim_cfg, 0, sizeof(sim_cfg));
    sim_cfg.delay_min_ms = 5;
    sim_cfg.delay_max_ms = 15;
    sim_cfg.reorder_rate = 0.20;
    sim_cfg.duplicate_rate = 0.20;

    rel4u_net_sim_t* sim = rel4u_net_sim_create(proxy_port, srv_port, &sim_cfg);
    ASSERT_TRUE(sim != NULL);
    ASSERT_EQ(rel4u_net_sim_start(sim), 0);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = proxy_port;
    c_cfg.send_queue_capacity = 1024;
    c_cfg.recv_queue_capacity = 1024;
    c_cfg.inactivity_timeout_ms = 15000;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    int num_messages = is_heavy_stress_mode() ? 200 : 80;

    for (int i = 0; i < num_messages; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "reorder_dup_msg_%04d", i);
        ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, msg, strlen(msg)), REL4U_OK);
    }

    char recv_buf[128];
    size_t recv_len = 0;
    uint32_t from_cid = 0;

    for (int i = 0; i < num_messages; i++) {
        char expected[64];
        snprintf(expected, sizeof(expected), "reorder_dup_msg_%04d", i);

        int res = rel4u_server_recv(server, &from_cid, recv_buf, sizeof(recv_buf), &recv_len, 10000);
        ASSERT_EQ(res, REL4U_OK);
        recv_buf[recv_len] = '\0';
        ASSERT_STR_EQ(recv_buf, expected);
    }

    rel4u_net_sim_stats_t sim_stats;
    rel4u_net_sim_get_stats(sim, &sim_stats);
    ASSERT_TRUE(sim_stats.client_packets_duplicated > 0 || sim_stats.server_packets_duplicated > 0);

    rel4u_client_destroy(client);
    rel4u_net_sim_destroy(sim);
    rel4u_server_destroy(server);
    return true;
}

static bool test_stress_multi_client_concurrent_loss(void) {
    uint16_t srv_port = 19117;
    uint16_t proxy_port = 19116;

    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = srv_port;
    s_cfg.max_clients = 8;
    s_cfg.send_queue_capacity = 1024;
    s_cfg.recv_queue_capacity = 1024;
    s_cfg.inactivity_timeout_ms = 15000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    // 15% packet drop + 5ms delay
    rel4u_net_sim_config_t sim_cfg;
    memset(&sim_cfg, 0, sizeof(sim_cfg));
    sim_cfg.drop_rate = 0.15;
    sim_cfg.delay_min_ms = 2;
    sim_cfg.delay_max_ms = 8;

    rel4u_net_sim_t* sim = rel4u_net_sim_create(proxy_port, srv_port, &sim_cfg);
    ASSERT_TRUE(sim != NULL);
    ASSERT_EQ(rel4u_net_sim_start(sim), 0);

    #define STRESS_NUM_CLIENTS 4
    rel4u_client_t* clients[STRESS_NUM_CLIENTS];

    for (int c = 0; c < STRESS_NUM_CLIENTS; c++) {
        rel4u_client_config_t c_cfg;
        memset(&c_cfg, 0, sizeof(c_cfg));
        c_cfg.server_address = "127.0.0.1";
        c_cfg.server_port = proxy_port;
        c_cfg.send_queue_capacity = 1024;
        c_cfg.recv_queue_capacity = 1024;
        c_cfg.inactivity_timeout_ms = 15000;
        c_cfg.connect_timeout_ms = 5000;

        clients[c] = rel4u_client_create(&c_cfg);
        ASSERT_TRUE(clients[c] != NULL);
        ASSERT_EQ(rel4u_client_connect(clients[c]), REL4U_OK);
    }

    int msgs_per_client = is_heavy_stress_mode() ? 100 : 30;

    for (int m = 0; m < msgs_per_client; m++) {
        for (int c = 0; c < STRESS_NUM_CLIENTS; c++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "c%d_seq_%04d", c, m);
            ASSERT_EQ(rel4u_client_send(clients[c], REL4U_MODE_RELIABLE_ORDERED, buf, strlen(buf)), REL4U_OK);
        }
    }

    int client_seq_counters[STRESS_NUM_CLIENTS];
    memset(client_seq_counters, 0, sizeof(client_seq_counters));

    int total_received = 0;
    int total_expected = STRESS_NUM_CLIENTS * msgs_per_client;
    char recv_buf[128];
    size_t recv_len = 0;
    uint32_t from_cid = 0;

    for (int i = 0; i < total_expected; i++) {
        int res = rel4u_server_recv(server, &from_cid, recv_buf, sizeof(recv_buf), &recv_len, 10000);
        ASSERT_EQ(res, REL4U_OK);
        recv_buf[recv_len] = '\0';

        int client_idx = -1;
        int seq_num = -1;
        int parsed = sscanf(recv_buf, "c%d_seq_%04d", &client_idx, &seq_num);
        ASSERT_EQ(parsed, 2);
        ASSERT_TRUE(client_idx >= 0 && client_idx < STRESS_NUM_CLIENTS);

        // Verify each client stream's in-order arrival
        ASSERT_EQ(seq_num, client_seq_counters[client_idx]);
        client_seq_counters[client_idx]++;
        total_received++;
    }

    ASSERT_EQ(total_received, total_expected);

    for (int c = 0; c < STRESS_NUM_CLIENTS; c++) {
        ASSERT_EQ(client_seq_counters[c], msgs_per_client);
        rel4u_client_destroy(clients[c]);
    }
    #undef STRESS_NUM_CLIENTS

    rel4u_net_sim_destroy(sim);
    rel4u_server_destroy(server);
    return true;
}

static bool test_stress_mixed_delivery_modes_under_loss(void) {
    uint16_t srv_port = 19119;
    uint16_t proxy_port = 19118;

    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = srv_port;
    s_cfg.max_clients = 4;
    s_cfg.send_queue_capacity = 1024;
    s_cfg.recv_queue_capacity = 1024;
    s_cfg.inactivity_timeout_ms = 15000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    // 20% loss
    rel4u_net_sim_config_t sim_cfg;
    memset(&sim_cfg, 0, sizeof(sim_cfg));
    sim_cfg.drop_rate = 0.20;

    rel4u_net_sim_t* sim = rel4u_net_sim_create(proxy_port, srv_port, &sim_cfg);
    ASSERT_TRUE(sim != NULL);
    ASSERT_EQ(rel4u_net_sim_start(sim), 0);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = proxy_port;
    c_cfg.send_queue_capacity = 1024;
    c_cfg.recv_queue_capacity = 1024;
    c_cfg.inactivity_timeout_ms = 15000;
    c_cfg.connect_timeout_ms = 5000;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    int count = is_heavy_stress_mode() ? 100 : 40;

    // Send interleaved Reliable Ordered, Reliable Unordered, and Unreliable Unordered
    for (int i = 0; i < count; i++) {
        char msg_ro[64], msg_ru[64], msg_uu[64];
        snprintf(msg_ro, sizeof(msg_ro), "RO_%04d", i);
        snprintf(msg_ru, sizeof(msg_ru), "RU_%04d", i);
        snprintf(msg_uu, sizeof(msg_uu), "UU_%04d", i);

        ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, msg_ro, strlen(msg_ro)), REL4U_OK);
        ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_UNORDERED, msg_ru, strlen(msg_ru)), REL4U_OK);
        rel4u_client_send(client, REL4U_MODE_UNRELIABLE_UNORDERED, msg_uu, strlen(msg_uu));
    }

    int ro_expected_seq = 0;
    bool ru_received[1024];
    memset(ru_received, 0, sizeof(ru_received));
    int ru_total_received = 0;

    char recv_buf[128];
    size_t recv_len = 0;
    uint32_t from_cid = 0;

    // We must receive all count RO messages and all count RU messages.
    // Some UU messages might be received or dropped.
    while (ro_expected_seq < count || ru_total_received < count) {
        int res = rel4u_server_recv(server, &from_cid, recv_buf, sizeof(recv_buf), &recv_len, 10000);
        ASSERT_EQ(res, REL4U_OK);
        recv_buf[recv_len] = '\0';

        if (strncmp(recv_buf, "RO_", 3) == 0) {
            int seq = atoi(recv_buf + 3);
            ASSERT_EQ(seq, ro_expected_seq);
            ro_expected_seq++;
        } else if (strncmp(recv_buf, "RU_", 3) == 0) {
            int seq = atoi(recv_buf + 3);
            ASSERT_TRUE(seq >= 0 && seq < count);
            if (!ru_received[seq]) {
                ru_received[seq] = true;
                ru_total_received++;
            }
        } else if (strncmp(recv_buf, "UU_", 3) == 0) {
            // Unreliable messages can arrive or be lost
        }
    }

    ASSERT_EQ(ro_expected_seq, count);
    ASSERT_EQ(ru_total_received, count);

    rel4u_client_destroy(client);
    rel4u_net_sim_destroy(sim);
    rel4u_server_destroy(server);
    return true;
}

static bool test_server_reset_and_client_auto_reconnect(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19120;
    s_cfg.max_clients = 4;
    s_cfg.inactivity_timeout_ms = 300; // Fast timeout for test
    s_cfg.heartbeat_interval_ms = 5000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19120;
    c_cfg.inactivity_timeout_ms = 10000; // Keep client alive to test Server RESET response
    c_cfg.heartbeat_interval_ms = 5000;
    c_cfg.reconnect_interval_ms = 100;
    c_cfg.disable_auto_reconnect = false;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);
    ASSERT_EQ(rel4u_client_get_state(client), REL4U_STATE_CONNECTED);

    // Initial message
    const char* m1 = "initial_message";
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, m1, strlen(m1)), REL4U_OK);

    char buf[128];
    size_t len = 0;
    uint32_t cid = 0;
    ASSERT_EQ(rel4u_server_recv(server, &cid, buf, sizeof(buf), &len, 1000), REL4U_OK);
    buf[len] = '\0';
    ASSERT_STR_EQ(buf, m1);

    // Wait for server to time out client session (300ms timeout)
    rel4u_time_sleep_ms(600);

    // Server should have evicted client session
    rel4u_stats_t s_stats;
    rel4u_server_get_stats(server, &s_stats);
    ASSERT_EQ(s_stats.active_connections, 0);

    // Now client sends a new reliable message.
    // Server will respond with RESET -> Client triggers auto-reconnect -> Delivers message!
    const char* m2 = "after_timeout_message";
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, m2, strlen(m2)), REL4U_OK);

    ASSERT_EQ(rel4u_server_recv(server, &cid, buf, sizeof(buf), &len, 3000), REL4U_OK);
    buf[len] = '\0';
    ASSERT_STR_EQ(buf, m2);
    ASSERT_EQ(rel4u_client_get_state(client), REL4U_STATE_CONNECTED);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

static bool test_client_disable_auto_reconnect(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19121;
    s_cfg.max_clients = 4;
    s_cfg.inactivity_timeout_ms = 300;
    s_cfg.heartbeat_interval_ms = 5000;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19121;
    c_cfg.inactivity_timeout_ms = 300;
    c_cfg.heartbeat_interval_ms = 5000;
    c_cfg.disable_auto_reconnect = true; // Opt out

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    // Wait for timeout (300ms timeout)
    rel4u_time_sleep_ms(600);

    // Should transition to DISCONNECTED and fail send
    ASSERT_EQ(rel4u_client_get_state(client), REL4U_STATE_DISCONNECTED);
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, "test", 4), REL4U_ERR_NOT_CONNECTED);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

static bool test_voluntary_server_disconnect_no_reconnect(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19122;
    s_cfg.max_clients = 4;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19122;
    c_cfg.disable_auto_reconnect = false; // Auto-reconnect enabled!

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    // Send a message so server registers client ID
    const char* m = "hello";
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, m, strlen(m)), REL4U_OK);

    char buf[128];
    size_t len = 0;
    uint32_t cid = 0;
    ASSERT_EQ(rel4u_server_recv(server, &cid, buf, sizeof(buf), &len, 1000), REL4U_OK);

    // Voluntary server-side disconnect (e.g. kicking the client)
    ASSERT_EQ(rel4u_server_disconnect_client(server, cid), REL4U_OK);

    // Give time for DISCONNECT packet to arrive and process
    rel4u_time_sleep_ms(100);

    // Client must be DISCONNECTED and NOT reconnecting
    ASSERT_EQ(rel4u_client_get_state(client), REL4U_STATE_DISCONNECTED);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

static bool test_dynamic_memory_mode(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19125;
    s_cfg.max_clients = 8;
    s_cfg.memory_mode = REL4U_MEM_DYNAMIC;
    s_cfg.queue_full_policy = REL4U_QUEUE_FULL_DROP_NEW;
    s_cfg.max_send_memory_bytes = 256 * 1024;
    s_cfg.max_recv_memory_bytes = 256 * 1024;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19125;
    c_cfg.memory_mode = REL4U_MEM_DYNAMIC;
    c_cfg.queue_full_policy = REL4U_QUEUE_FULL_DROP_NEW;
    c_cfg.max_send_memory_bytes = 256 * 1024;
    c_cfg.max_recv_memory_bytes = 256 * 1024;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    const char* test_msg = "hello_dynamic_memory";
    ASSERT_EQ(rel4u_client_send(client, REL4U_MODE_RELIABLE_ORDERED, test_msg, strlen(test_msg)), REL4U_OK);

    char buf[128];
    size_t len = 0;
    uint32_t cid = 0;
    ASSERT_EQ(rel4u_server_recv(server, &cid, buf, sizeof(buf), &len, 1000), REL4U_OK);
    buf[len] = '\0';
    ASSERT_STR_EQ(buf, test_msg);

    // Echo back from server to client
    const char* reply_msg = "reply_dynamic_memory";
    ASSERT_EQ(rel4u_server_send(server, cid, REL4U_MODE_RELIABLE_ORDERED, reply_msg, strlen(reply_msg)), REL4U_OK);

    size_t clen = 0;
    ASSERT_EQ(rel4u_client_recv(client, buf, sizeof(buf), &clen, 1000), REL4U_OK);
    buf[clen] = '\0';
    ASSERT_STR_EQ(buf, reply_msg);

    rel4u_stats_t c_stats, s_stats;
    ASSERT_EQ(rel4u_client_get_stats(client, &c_stats), REL4U_OK);
    ASSERT_EQ(rel4u_server_get_stats(server, &s_stats), REL4U_OK);
    ASSERT_EQ(c_stats.packets_dropped_queue_full, 0);
    ASSERT_EQ(s_stats.packets_dropped_queue_full, 0);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

static bool test_drop_last_queue_policy(void) {
    rel4u_server_config_t s_cfg;
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.bind_port = 19126;
    s_cfg.max_clients = 8;
    s_cfg.memory_mode = REL4U_MEM_FIXED;
    s_cfg.queue_full_policy = REL4U_QUEUE_FULL_DROP_LAST;
    s_cfg.send_queue_capacity = 8;
    s_cfg.recv_queue_capacity = 8;

    rel4u_server_t* server = rel4u_server_create(&s_cfg);
    ASSERT_TRUE(server != NULL);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);

    rel4u_client_config_t c_cfg;
    memset(&c_cfg, 0, sizeof(c_cfg));
    c_cfg.server_address = "127.0.0.1";
    c_cfg.server_port = 19126;
    c_cfg.memory_mode = REL4U_MEM_FIXED;
    c_cfg.queue_full_policy = REL4U_QUEUE_FULL_DROP_LAST;
    c_cfg.send_queue_capacity = 8;
    c_cfg.recv_queue_capacity = 8;

    rel4u_client_t* client = rel4u_client_create(&c_cfg);
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);

    // Rapidly send 50 messages, which far exceeds send_queue_capacity (8)
    // Under DROP_LAST, send should never return REL4U_ERR_QUEUE_FULL because oldest are evicted.
    for (int i = 0; i < 50; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "msg_%d", i);
        int res = rel4u_client_send(client, REL4U_MODE_UNRELIABLE_UNORDERED, msg, strlen(msg));
        ASSERT_EQ(res, REL4U_OK);
    }

    rel4u_time_sleep_ms(100);

    rel4u_stats_t stats;
    ASSERT_EQ(rel4u_client_get_stats(client, &stats), REL4U_OK);
    ASSERT_TRUE(stats.packets_dropped_queue_full > 0);

    rel4u_client_destroy(client);
    rel4u_server_destroy(server);
    return true;
}

TEST_SUITE_BEGIN("rel4u Integration Tests")
    RUN_TEST(test_client_server_connect_disconnect);
    RUN_TEST(test_all_delivery_modes);
    RUN_TEST(test_multi_client_concurrent);
    RUN_TEST(test_server_max_clients_rejection);
    RUN_TEST(test_server_max_clients_lru_eviction);
    RUN_TEST(test_stats_query);
    RUN_TEST(test_server_reset_and_client_auto_reconnect);
    RUN_TEST(test_client_disable_auto_reconnect);
    RUN_TEST(test_voluntary_server_disconnect_no_reconnect);
    RUN_TEST(test_dynamic_memory_mode);
    RUN_TEST(test_drop_last_queue_policy);
    RUN_TEST(test_stress_reliable_ordered_heavy_loss);
    RUN_TEST(test_stress_burst_loss_blackout_recovery);
    RUN_TEST(test_stress_packet_reordering_and_duplication);
    RUN_TEST(test_stress_multi_client_concurrent_loss);
    RUN_TEST(test_stress_mixed_delivery_modes_under_loss);
TEST_SUITE_END()

