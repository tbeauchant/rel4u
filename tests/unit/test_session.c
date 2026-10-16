#include "../framework/test_framework.h"
#include "../../src/core/rel4u_session.h"
#include "../../src/platform/rel4u_net.h"

#include "../framework/test_resources.h"

static bool test_session_table_basic(void) {
    rel4u_net_init();

    rel4u_session_table_t table;
    ASSERT_EQ(rel4u_session_table_init(&table, 4, REL4U_MAX_CLIENTS_REJECT, 16), 0);
    ASSERT_EQ(table.max_clients, 4);
    ASSERT_EQ(table.active_count, 0);

    rel4u_net_addr_t addr1, addr2;
    ASSERT_EQ(rel4u_net_addr_from_string(&addr1, "127.0.0.1", 10001), 0);
    ASSERT_EQ(rel4u_net_addr_from_string(&addr2, "127.0.0.1", 10002), 0);

    bool evicted = false;
    rel4u_client_session_t* s1 = rel4u_session_allocate(&table, &addr1, 0x1111, &evicted);
    ASSERT_TRUE(s1 != NULL);
    ASSERT_FALSE(evicted);
    ASSERT_EQ(table.active_count, 1);
    ASSERT_EQ(s1->session_id, 0x1111);

    rel4u_client_session_t* s2 = rel4u_session_allocate(&table, &addr2, 0x2222, &evicted);
    ASSERT_TRUE(s2 != NULL);
    ASSERT_FALSE(evicted);
    ASSERT_EQ(table.active_count, 2);
    ASSERT_EQ(s2->session_id, 0x2222);

    ASSERT_EQ(rel4u_session_find_by_addr(&table, &addr1), s1);
    ASSERT_EQ(rel4u_session_find_by_addr(&table, &addr2), s2);
    ASSERT_EQ(rel4u_session_find_by_id(&table, s1->client_id), s1);
    ASSERT_EQ(rel4u_session_find_by_id(&table, s2->client_id), s2);

    /* Free s1 and verify lookup fails */
    rel4u_session_free(&table, s1);
    ASSERT_EQ(table.active_count, 1);
    ASSERT_TRUE(rel4u_session_find_by_addr(&table, &addr1) == NULL);
    ASSERT_TRUE(rel4u_session_find_by_id(&table, s1->client_id) == NULL);

    /* Double free of already disconnected session is a safe no-op */
    rel4u_session_free(&table, s1);
    ASSERT_EQ(table.active_count, 1);

    rel4u_session_table_destroy(&table);
    ASSERT_EQ(table.active_count, 0);
    return true;
}

static bool test_session_table_reject_policy(void) {
    rel4u_net_init();

    rel4u_session_table_t table;
    ASSERT_EQ(rel4u_session_table_init(&table, 2, REL4U_MAX_CLIENTS_REJECT, 16), 0);

    rel4u_net_addr_t a1, a2, a3;
    ASSERT_EQ(rel4u_net_addr_from_string(&a1, "127.0.0.1", 10001), 0);
    ASSERT_EQ(rel4u_net_addr_from_string(&a2, "127.0.0.1", 10002), 0);
    ASSERT_EQ(rel4u_net_addr_from_string(&a3, "127.0.0.1", 10003), 0);

    bool evicted = false;
    rel4u_client_session_t* s1 = rel4u_session_allocate(&table, &a1, 1, &evicted);
    ASSERT_TRUE(s1 != NULL);
    ASSERT_FALSE(evicted);

    rel4u_client_session_t* s2 = rel4u_session_allocate(&table, &a2, 2, &evicted);
    ASSERT_TRUE(s2 != NULL);
    ASSERT_FALSE(evicted);
    ASSERT_EQ(table.active_count, 2);

    /* 3rd allocation must be rejected */
    rel4u_client_session_t* s3 = rel4u_session_allocate(&table, &a3, 3, &evicted);
    ASSERT_TRUE(s3 == NULL);
    ASSERT_FALSE(evicted);
    ASSERT_EQ(table.active_count, 2);

    rel4u_session_table_destroy(&table);
    return true;
}

static bool test_session_table_lru_eviction(void) {
    rel4u_net_init();

    rel4u_session_table_t table;
    ASSERT_EQ(rel4u_session_table_init(&table, 3, REL4U_MAX_CLIENTS_DROP_OLD, 16), 0);

    rel4u_net_addr_t a1, a2, a3, a4;
    ASSERT_EQ(rel4u_net_addr_from_string(&a1, "127.0.0.1", 20001), 0);
    ASSERT_EQ(rel4u_net_addr_from_string(&a2, "127.0.0.1", 20002), 0);
    ASSERT_EQ(rel4u_net_addr_from_string(&a3, "127.0.0.1", 20003), 0);
    ASSERT_EQ(rel4u_net_addr_from_string(&a4, "127.0.0.1", 20004), 0);

    bool evicted = false;
    rel4u_client_session_t* s1 = rel4u_session_allocate(&table, &a1, 10, &evicted);
    ASSERT_TRUE(s1 != NULL);
    rel4u_session_touch_lru(&table, s1, 100);

    rel4u_client_session_t* s2 = rel4u_session_allocate(&table, &a2, 20, &evicted);
    ASSERT_TRUE(s2 != NULL);
    rel4u_session_touch_lru(&table, s2, 200);

    rel4u_client_session_t* s3 = rel4u_session_allocate(&table, &a3, 30, &evicted);
    ASSERT_TRUE(s3 != NULL);
    rel4u_session_touch_lru(&table, s3, 300);

    /* Touch s1 at t=400 so s2 (t=200) becomes the oldest LRU entry */
    rel4u_session_touch_lru(&table, s1, 400);

    rel4u_client_session_t* s4 = rel4u_session_allocate(&table, &a4, 40, &evicted);
    ASSERT_TRUE(s4 != NULL);
    ASSERT_TRUE(evicted);
    ASSERT_EQ(table.active_count, 3);

    /* s2 must be evicted, while a1, a3, and a4 remain active */
    ASSERT_TRUE(rel4u_session_find_by_addr(&table, &a2) == NULL);
    ASSERT_TRUE(rel4u_session_find_by_addr(&table, &a1) != NULL);
    ASSERT_TRUE(rel4u_session_find_by_addr(&table, &a3) != NULL);
    ASSERT_TRUE(rel4u_session_find_by_addr(&table, &a4) != NULL);

    rel4u_session_table_destroy(&table);
    return true;
}

static bool test_session_table_edge_cases(void) {
    rel4u_session_table_t table;
    ASSERT_EQ(rel4u_session_table_init(NULL, 4, REL4U_MAX_CLIENTS_REJECT, 16), -1);
    ASSERT_EQ(rel4u_session_table_init(&table, 0, REL4U_MAX_CLIENTS_REJECT, 16), -1);
    /* Non-power-of-2 or < 2 window_size must fail initialization */
    ASSERT_EQ(rel4u_session_table_init(&table, 4, REL4U_MAX_CLIENTS_REJECT, 1), -1);
    ASSERT_EQ(rel4u_session_table_init(&table, 4, REL4U_MAX_CLIENTS_REJECT, 10), -1);

    ASSERT_EQ(rel4u_session_table_init(&table, 2, REL4U_MAX_CLIENTS_REJECT, 16), 0);

    ASSERT_TRUE(rel4u_session_find_by_addr(NULL, NULL) == NULL);
    ASSERT_TRUE(rel4u_session_find_by_addr(&table, NULL) == NULL);
    ASSERT_TRUE(rel4u_session_find_by_id(NULL, 0) == NULL);
    ASSERT_TRUE(rel4u_session_find_by_id(&table, 999) == NULL);
    ASSERT_TRUE(rel4u_session_allocate(NULL, NULL, 0, NULL) == NULL);

    rel4u_session_touch_lru(NULL, NULL, 100);
    rel4u_session_free(NULL, NULL);
    rel4u_session_table_destroy(NULL);
    rel4u_session_table_destroy(&table);
    return true;
}

TEST_SUITE_BEGIN("Session Table Unit Tests")
    RUN_TEST(test_session_table_basic);
    RUN_TEST(test_session_table_reject_policy);
    RUN_TEST(test_session_table_lru_eviction);
    RUN_TEST(test_session_table_edge_cases);
TEST_SUITE_END()

