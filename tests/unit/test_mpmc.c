#include "../framework/test_framework.h"
#include "../../src/core/rel4u_mpmc.h"
#include "../../src/platform/rel4u_threads.h"
#include <stdlib.h>
#include <stdatomic.h>

static bool test_mpmc_basic(void) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 8, 0), 0);

    rel4u_mpmc_item_t item;
    memset(&item, 0, sizeof(item));

    // Initially empty
    ASSERT_FALSE(rel4u_mpmc_try_pop(&q, &item));

    // Push 8 items (fill queue)
    for (uint32_t i = 0; i < 8; i++) {
        item.client_id = i + 100;
        item.len = 4;
        ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    }

    // 9th push should fail (queue full)
    item.client_id = 999;
    ASSERT_FALSE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);

    // Pop all 8 items in FIFO order
    for (uint32_t i = 0; i < 8; i++) {
        ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &item));
        ASSERT_EQ(item.client_id, i + 100);
    }

    // Now empty again
    ASSERT_FALSE(rel4u_mpmc_try_pop(&q, &item));

    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_fixed_drop_last(void) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_LAST, 4, 0), 0);

    rel4u_mpmc_item_t item;
    memset(&item, 0, sizeof(item));

    // Push 4 items: 10, 20, 30, 40
    for (uint32_t i = 1; i <= 4; i++) {
        item.client_id = i * 10;
        ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    }
    ASSERT_EQ(rel4u_mpmc_size(&q), 4);
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 0);

    // 5th push: item 50 should evict oldest (10)
    item.client_id = 50;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_EQ(rel4u_mpmc_size(&q), 4);
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);

    // 6th push: item 60 should evict oldest (20)
    item.client_id = 60;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_EQ(rel4u_mpmc_size(&q), 4);
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 2);

    // Queue should now contain: 30, 40, 50, 60
    uint32_t expected[] = {30, 40, 50, 60};
    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &item));
        ASSERT_EQ(item.client_id, expected[i]);
    }
    ASSERT_FALSE(rel4u_mpmc_try_pop(&q, &item));

    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_dynamic_drop_new(void) {
    rel4u_mpmc_queue_t q;
    size_t node_sz = sizeof(rel4u_mpmc_node_t);
    // Allow exactly 3 items of 16 bytes payload
    size_t max_bytes = (node_sz + 16) * 3;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_DYNAMIC, REL4U_QUEUE_FULL_DROP_NEW, 0, max_bytes), 0);

    rel4u_mpmc_item_t item;
    memset(&item, 0, sizeof(item));
    item.len = 16;

    for (uint32_t i = 1; i <= 3; i++) {
        item.client_id = i;
        ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    }
    ASSERT_EQ(rel4u_mpmc_size(&q), 3);
    ASSERT_EQ(rel4u_mpmc_bytes(&q), max_bytes);

    // 4th push should fail
    item.client_id = 4;
    ASSERT_FALSE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);

    // Pop first item, then 4th item can be pushed
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &item));
    ASSERT_EQ(item.client_id, 1);
    ASSERT_EQ(rel4u_mpmc_size(&q), 2);

    item.client_id = 4;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_EQ(rel4u_mpmc_size(&q), 3);

    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_dynamic_drop_last(void) {
    rel4u_mpmc_queue_t q;
    size_t node_sz = sizeof(rel4u_mpmc_node_t);
    size_t max_bytes = (node_sz + 16) * 3;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_DYNAMIC, REL4U_QUEUE_FULL_DROP_LAST, 0, max_bytes), 0);

    rel4u_mpmc_item_t item;
    memset(&item, 0, sizeof(item));
    item.len = 16;

    for (uint32_t i = 1; i <= 3; i++) {
        item.client_id = i * 10;
        ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    }
    ASSERT_EQ(rel4u_mpmc_size(&q), 3);
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 0);

    // 4th push: item 40 should evict oldest (10)
    item.client_id = 40;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_EQ(rel4u_mpmc_size(&q), 3);
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);

    // Pop should yield: 20, 30, 40
    uint32_t expected[] = {20, 30, 40};
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &item));
        ASSERT_EQ(item.client_id, expected[i]);
    }
    ASSERT_EQ(rel4u_mpmc_size(&q), 0);
    ASSERT_EQ(rel4u_mpmc_bytes(&q), 0);

    rel4u_mpmc_destroy(&q);
    return true;
}

#define THREAD_STRESS_ITEMS 50000
#define NUM_PRODUCERS 4
#define NUM_CONSUMERS 4

typedef struct thread_arg {
    rel4u_mpmc_queue_t* q;
    uint32_t thread_id;
    atomic_uint_fast64_t* total_consumed_sum;
    atomic_uint_fast32_t* total_consumed_count;
} thread_arg_t;

static void* producer_func(void* arg) {
    thread_arg_t* t = (thread_arg_t*)arg;
    for (uint32_t i = 1; i <= THREAD_STRESS_ITEMS; i++) {
        rel4u_mpmc_item_t item;
        item.client_id = t->thread_id;
        item.len = (uint16_t)((i % 256) + sizeof(uint32_t));
        *(uint32_t*)item.data = i;

        while (!rel4u_mpmc_push_notify(t->q, &item)) {
            // Spin / yield if full
        }
    }
    return NULL;
}

static void* consumer_func(void* arg) {
    thread_arg_t* t = (thread_arg_t*)arg;
    uint32_t target_total = THREAD_STRESS_ITEMS * NUM_PRODUCERS;

    while (atomic_load(t->total_consumed_count) < target_total) {
        rel4u_mpmc_item_t item;
        if (rel4u_mpmc_pop_wait(t->q, &item, 10)) {
            uint32_t val = *(uint32_t*)item.data;
            atomic_fetch_add(t->total_consumed_sum, val);
            atomic_fetch_add(t->total_consumed_count, 1);
        }
    }
    return NULL;
}

static bool test_mpmc_multithreaded_stress(void) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 1024, 0), 0);

    atomic_uint_fast64_t total_consumed_sum = 0;
    atomic_uint_fast32_t total_consumed_count = 0;

    rel4u_thread_t producers[NUM_PRODUCERS];
    rel4u_thread_t consumers[NUM_CONSUMERS];
    thread_arg_t prod_args[NUM_PRODUCERS];
    thread_arg_t cons_args[NUM_CONSUMERS];

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        cons_args[i].q = &q;
        cons_args[i].thread_id = (uint32_t)i;
        cons_args[i].total_consumed_sum = &total_consumed_sum;
        cons_args[i].total_consumed_count = &total_consumed_count;
        rel4u_thread_create(&consumers[i], consumer_func, &cons_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        prod_args[i].q = &q;
        prod_args[i].thread_id = (uint32_t)i;
        prod_args[i].total_consumed_sum = &total_consumed_sum;
        prod_args[i].total_consumed_count = &total_consumed_count;
        rel4u_thread_create(&producers[i], producer_func, &prod_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        rel4u_thread_join(producers[i]);
    }

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        rel4u_thread_join(consumers[i]);
    }

    uint64_t expected_per_prod = ((uint64_t)THREAD_STRESS_ITEMS * (THREAD_STRESS_ITEMS + 1)) / 2;
    uint64_t expected_total_sum = expected_per_prod * NUM_PRODUCERS;
    uint32_t expected_total_count = THREAD_STRESS_ITEMS * NUM_PRODUCERS;

    ASSERT_EQ(atomic_load(&total_consumed_count), expected_total_count);
    ASSERT_EQ(atomic_load(&total_consumed_sum), expected_total_sum);

    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_dynamic_stress(void) {
    rel4u_mpmc_queue_t q;
    // Allow plenty of heap space for dynamic stress
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_DYNAMIC, REL4U_QUEUE_FULL_DROP_NEW, 0, 8 * 1024 * 1024), 0);

    atomic_uint_fast64_t total_consumed_sum = 0;
    atomic_uint_fast32_t total_consumed_count = 0;

    rel4u_thread_t producers[NUM_PRODUCERS];
    rel4u_thread_t consumers[NUM_CONSUMERS];
    thread_arg_t prod_args[NUM_PRODUCERS];
    thread_arg_t cons_args[NUM_CONSUMERS];

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        cons_args[i].q = &q;
        cons_args[i].thread_id = (uint32_t)i;
        cons_args[i].total_consumed_sum = &total_consumed_sum;
        cons_args[i].total_consumed_count = &total_consumed_count;
        rel4u_thread_create(&consumers[i], consumer_func, &cons_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        prod_args[i].q = &q;
        prod_args[i].thread_id = (uint32_t)i;
        prod_args[i].total_consumed_sum = &total_consumed_sum;
        prod_args[i].total_consumed_count = &total_consumed_count;
        rel4u_thread_create(&producers[i], producer_func, &prod_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
        rel4u_thread_join(producers[i]);
    }

    for (int i = 0; i < NUM_CONSUMERS; i++) {
        rel4u_thread_join(consumers[i]);
    }

    uint64_t expected_per_prod = ((uint64_t)THREAD_STRESS_ITEMS * (THREAD_STRESS_ITEMS + 1)) / 2;
    uint64_t expected_total_sum = expected_per_prod * NUM_PRODUCERS;
    uint32_t expected_total_count = THREAD_STRESS_ITEMS * NUM_PRODUCERS;

    ASSERT_EQ(atomic_load(&total_consumed_count), expected_total_count);
    ASSERT_EQ(atomic_load(&total_consumed_sum), expected_total_sum);
    ASSERT_EQ(rel4u_mpmc_bytes(&q), 0);

    rel4u_mpmc_destroy(&q);
    return true;
}

TEST_SUITE_BEGIN("Thread-Safe Bounded MPMC Queue Unit Tests")
    RUN_TEST(test_mpmc_basic);
    RUN_TEST(test_mpmc_fixed_drop_last);
    RUN_TEST(test_mpmc_dynamic_drop_new);
    RUN_TEST(test_mpmc_dynamic_drop_last);
    RUN_TEST(test_mpmc_multithreaded_stress);
    RUN_TEST(test_mpmc_dynamic_stress);
TEST_SUITE_END()
