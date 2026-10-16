#include "../framework/test_framework.h"
#include "../../include/rel4u.h"
#include <string.h>

#include "../framework/test_resources.h"

static bool test_client_api_validation(void) {
    /* 1. Invalid creation configs */
    ASSERT_TRUE(rel4u_client_create(NULL) == NULL);

    rel4u_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_TRUE(rel4u_client_create(&cfg) == NULL); /* Missing address and port */

    cfg.server_address = "127.0.0.1";
    cfg.server_port = 0;
    ASSERT_TRUE(rel4u_client_create(&cfg) == NULL); /* Port 0 invalid */

    cfg.server_port = 19999;
    cfg.window_size = 10; /* Non-power-of-2 window_size >= 8 must fail */
    ASSERT_TRUE(rel4u_client_create(&cfg) == NULL);

    cfg.window_size = 16;
    rel4u_client_t* c = rel4u_client_create(&cfg);
    ASSERT_TRUE(c != NULL);

    /* 2. State & Stats validation */
    ASSERT_EQ(rel4u_client_get_state(NULL), REL4U_STATE_DISCONNECTED);
    ASSERT_EQ(rel4u_client_get_state(c), REL4U_STATE_DISCONNECTED);

    rel4u_stats_t stats;
    ASSERT_EQ(rel4u_client_get_stats(NULL, &stats), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_get_stats(c, NULL), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_get_stats(c, &stats), REL4U_OK);

    /* 3. Connect / Send validation while disconnected */
    ASSERT_EQ(rel4u_client_connect(NULL), REL4U_ERR_INVALID_PARAM);

    char buf[16] = "hello";
    size_t max_p = REL4U_MAX_PAYLOAD(REL4U_DEFAULT_MTU);
    ASSERT_EQ(rel4u_client_send(NULL, REL4U_MODE_RELIABLE_ORDERED, buf, 5), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_send(c, REL4U_MODE_RELIABLE_ORDERED, NULL, 5), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_send(c, REL4U_MODE_RELIABLE_ORDERED, buf, 5), REL4U_ERR_NOT_CONNECTED);
    ASSERT_EQ(rel4u_client_send(c, REL4U_MODE_RELIABLE_ORDERED, buf, max_p + 1), REL4U_ERR_MSG_TOO_LARGE);

    /* 4. Recv validation */
    size_t out_len = 0;
    ASSERT_EQ(rel4u_client_recv(NULL, buf, sizeof(buf), &out_len, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_recv(c, NULL, sizeof(buf), &out_len, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_recv(c, buf, 0, &out_len, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_recv(c, buf, sizeof(buf), &out_len, 0), REL4U_ERR_QUEUE_EMPTY);
    ASSERT_EQ(rel4u_client_recv(c, buf, sizeof(buf), &out_len, 20), REL4U_ERR_TIMEOUT);

    /* 5. Disconnect & Destroy validation */
    ASSERT_EQ(rel4u_client_disconnect(NULL), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_client_disconnect(c), REL4U_OK);

    rel4u_client_destroy(NULL);
    rel4u_client_destroy(c);
    return true;
}

static bool test_server_api_validation(void) {
    /* 1. Invalid creation configs */
    ASSERT_TRUE(rel4u_server_create(NULL) == NULL);

    rel4u_server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_TRUE(rel4u_server_create(&cfg) == NULL); /* Port 0 invalid */

    cfg.bind_port = 19998;
    cfg.window_size = 10; /* Non-power-of-2 window_size >= 8 must fail */
    ASSERT_TRUE(rel4u_server_create(&cfg) == NULL);

    cfg.window_size = 16;
    rel4u_server_t* s = rel4u_server_create(&cfg);
    ASSERT_TRUE(s != NULL);

    /* 2. Start / Stats validation */
    ASSERT_EQ(rel4u_server_start(NULL), REL4U_ERR_INVALID_PARAM);

    rel4u_stats_t stats;
    ASSERT_EQ(rel4u_server_get_stats(NULL, &stats), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_get_stats(s, NULL), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_get_stats(s, &stats), REL4U_OK);

    ASSERT_EQ(rel4u_server_get_client_stats(NULL, 0, &stats), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_get_client_stats(s, 0, NULL), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_get_client_stats(s, 999, &stats), REL4U_ERR_CLIENT_NOT_FOUND);

    /* 3. Send validation while stopped */
    char buf[16] = "hello";
    size_t max_p = REL4U_MAX_PAYLOAD(REL4U_DEFAULT_MTU);
    ASSERT_EQ(rel4u_server_send(NULL, 0, REL4U_MODE_RELIABLE_ORDERED, buf, 5), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_send(s, 0, REL4U_MODE_RELIABLE_ORDERED, NULL, 5), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_send(s, 0, REL4U_MODE_RELIABLE_ORDERED, buf, 5), REL4U_ERR_NOT_CONNECTED);
    ASSERT_EQ(rel4u_server_send(s, 0, REL4U_MODE_RELIABLE_ORDERED, buf, max_p + 1), REL4U_ERR_MSG_TOO_LARGE);

    /* 4. Recv validation */
    uint32_t cid = 0;
    size_t out_len = 0;
    ASSERT_EQ(rel4u_server_recv(NULL, &cid, buf, sizeof(buf), &out_len, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_recv(s, &cid, NULL, sizeof(buf), &out_len, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_recv(s, &cid, buf, 0, &out_len, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_recv(s, &cid, buf, sizeof(buf), &out_len, 0), REL4U_ERR_QUEUE_EMPTY);
    ASSERT_EQ(rel4u_server_recv(s, &cid, buf, sizeof(buf), &out_len, 20), REL4U_ERR_TIMEOUT);

    /* 5. Disconnect client & Stop / Destroy validation */
    ASSERT_EQ(rel4u_server_disconnect_client(NULL, 0), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_disconnect_client(s, 999), REL4U_ERR_CLIENT_NOT_FOUND);
    ASSERT_EQ(rel4u_server_stop(NULL), REL4U_ERR_INVALID_PARAM);
    ASSERT_EQ(rel4u_server_stop(s), REL4U_OK);

    rel4u_server_destroy(NULL);
    rel4u_server_destroy(s);
    return true;
}

TEST_SUITE_BEGIN("Public API Negative and Boundary Unit Tests")
    RUN_TEST(test_client_api_validation);
    RUN_TEST(test_server_api_validation);
TEST_SUITE_END()

