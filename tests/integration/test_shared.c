#include "../framework/test_framework.h"
#include "rel4u.h"

static void destroy_client(void* object) {
    rel4u_client_destroy((rel4u_client_t*)object);
}

static void destroy_server(void* object) {
    rel4u_server_destroy((rel4u_server_t*)object);
}

static bool test_shared_library_api(void) {
    rel4u_server_config_t sc = {0};
    sc.bind_address = "127.0.0.1";
    sc.bind_port = 19224;
    sc.max_clients = 1;
    rel4u_server_t* server = rel4u_server_create(&sc);
    ASSERT_TRUE(server != NULL);
    test_cleanup_register(server, destroy_server);
    ASSERT_EQ(rel4u_server_start(server), REL4U_OK);
    rel4u_client_config_t cc = {0};
    cc.server_address = "127.0.0.1";
    cc.server_port = sc.bind_port;
    rel4u_client_t* client = rel4u_client_create(&cc);
    ASSERT_TRUE(client != NULL);
    test_cleanup_register(client, destroy_client);
    ASSERT_EQ(rel4u_client_connect(client), REL4U_OK);
    ASSERT_EQ(rel4u_client_get_state(client), REL4U_STATE_CONNECTED);
    uint32_t cid = 0;
    for (int mode = 0; mode < 4; mode++) {
        const uint8_t payload[] = {0, 255, 1, (uint8_t)mode};
        uint8_t received[sizeof(payload)];
        size_t len = 0;
        ASSERT_EQ(rel4u_client_send(client, (rel4u_delivery_mode_t)mode, payload, sizeof(payload)), REL4U_OK);
        ASSERT_EQ(rel4u_server_recv(server, &cid, received, sizeof(received), &len, 1000), REL4U_OK);
        ASSERT_EQ(len, sizeof(payload));
        ASSERT_EQ(memcmp(received, payload, len), 0);
        ASSERT_EQ(rel4u_server_send(server, cid, (rel4u_delivery_mode_t)mode, payload, sizeof(payload)), REL4U_OK);
        ASSERT_EQ(rel4u_client_recv(client, received, sizeof(received), &len, 1000), REL4U_OK);
        ASSERT_EQ(len, sizeof(payload));
        ASSERT_EQ(memcmp(received, payload, len), 0);
    }
    rel4u_stats_t stats;
    ASSERT_EQ(rel4u_server_get_stats(server, &stats), REL4U_OK);
    ASSERT_EQ(stats.active_connections, 1);
    ASSERT_EQ(rel4u_server_get_client_stats(server, cid, &stats), REL4U_OK);
    ASSERT_EQ(rel4u_client_get_stats(client, &stats), REL4U_OK);
    ASSERT_EQ(rel4u_server_disconnect_client(server, cid), REL4U_OK);
    ASSERT_EQ(rel4u_client_disconnect(client), REL4U_OK);
    ASSERT_EQ(rel4u_server_stop(server), REL4U_OK);
    test_cleanup_remove(client);
    rel4u_client_destroy(client);
    test_cleanup_remove(server);
    rel4u_server_destroy(server);
    return true;
}

TEST_SUITE_BEGIN("Shared library public API")
    RUN_TEST(test_shared_library_api);
TEST_SUITE_END()
