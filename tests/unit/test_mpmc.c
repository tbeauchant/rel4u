#include "../framework/test_framework.h"
#include "../../src/core/rel4u_mpmc.h"
#include "../../src/platform/rel4u_threads.h"
#include "../../src/platform/rel4u_time.h"
#include <stdlib.h>
#include <stdatomic.h>

#include "../framework/test_resources.h"

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

typedef struct stress_state {
    rel4u_mpmc_queue_t* q;
    atomic_uint consumed;
    atomic_bool stop;
    atomic_bool failed;
    atomic_uchar* seen;
    uint64_t deadline_ms;
    rel4u_thread_t threads[NUM_PRODUCERS + NUM_CONSUMERS];
    size_t threads_started;
} stress_state_t;

typedef struct thread_arg {
    stress_state_t* state;
    uint32_t producer;
} thread_arg_t;

static bool stress_should_stop(stress_state_t* state) {
    if (rel4u_time_now_ms() >= state->deadline_ms) {
        atomic_store(&state->failed, true);
        atomic_store(&state->stop, true);
    }
    return atomic_load(&state->stop);
}

static uint8_t stress_byte(uint32_t producer, uint32_t sequence, size_t offset) {
    return (uint8_t)(producer * 37 + sequence * 13 + offset);
}

static void* producer_func(void* arg) {
    thread_arg_t* t = (thread_arg_t*)arg;
    for (uint32_t i = 1; i <= THREAD_STRESS_ITEMS && !stress_should_stop(t->state); i++) {
        rel4u_mpmc_item_t item = {0};
        item.client_id = t->producer;
        item.mode = (uint8_t)(t->producer % 4);
        item.len = (uint16_t)((i % 256) + sizeof(uint32_t));
        memcpy(item.data, &i, sizeof(i));
        for (size_t j = sizeof(i); j < item.len; j++) {
            item.data[j] = stress_byte(t->producer, i, j);
        }
        while (!rel4u_mpmc_push_notify(t->state->q, &item)) {
            if (stress_should_stop(t->state)) return NULL;
            rel4u_time_sleep_ms(1);
        }
    }
    return NULL;
}

static void* consumer_func(void* arg) {
    stress_state_t* state = (stress_state_t*)arg;
    const uint32_t target = THREAD_STRESS_ITEMS * NUM_PRODUCERS;
    while (atomic_load(&state->consumed) < target && !stress_should_stop(state)) {
        rel4u_mpmc_item_t item;
        if (!rel4u_mpmc_pop_wait(state->q, &item, 10)) continue;
        uint32_t sequence;
        memcpy(&sequence, item.data, sizeof(sequence));
        bool valid = item.client_id < NUM_PRODUCERS && sequence >= 1 &&
                     sequence <= THREAD_STRESS_ITEMS &&
                     item.mode == item.client_id % 4 &&
                     item.len == (sequence % 256) + sizeof(sequence);
        for (size_t j = sizeof(sequence); valid && j < item.len; j++) {
            valid = item.data[j] == stress_byte(item.client_id, sequence, j);
        }
        if (valid) {
            size_t identity = item.client_id * THREAD_STRESS_ITEMS + sequence - 1;
            valid = atomic_exchange(&state->seen[identity], 1) == 0;
        }
        if (!valid) {
            atomic_store(&state->failed, true);
            atomic_store(&state->stop, true);
            break;
        }
        atomic_fetch_add(&state->consumed, 1);
    }
    return NULL;
}

static void stress_cleanup(void* object) {
    stress_state_t* state = (stress_state_t*)object;
    atomic_store(&state->stop, true);
    rel4u_mpmc_wake_all(state->q);
    for (size_t i = 0; i < state->threads_started; i++) {
        rel4u_thread_join(state->threads[i]);
    }
    state->threads_started = 0;
}

static bool run_mpmc_stress(rel4u_memory_mode_t mode) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(&q, mode, REL4U_QUEUE_FULL_DROP_NEW, 1024, 8 * 1024 * 1024), 0);
    const uint32_t target = THREAD_STRESS_ITEMS * NUM_PRODUCERS;
    /* Keep the allocation handle unqualified for MSVC's void* cleanup APIs. */
    void* seen_allocation = calloc(target, sizeof(atomic_uchar));
    ASSERT_TRUE(seen_allocation != NULL);
    atomic_uchar* seen = (atomic_uchar*)seen_allocation;
    for (uint32_t i = 0; i < target; i++) atomic_init(&seen[i], 0);
    test_cleanup_register(seen_allocation, free);
    stress_state_t state = {0};
    state.q = &q;
    state.seen = seen;
    state.deadline_ms = rel4u_time_now_ms() + 30000;
    atomic_init(&state.stop, false);
    atomic_init(&state.failed, false);
    atomic_init(&state.consumed, 0);
    test_cleanup_register(&state, stress_cleanup);
    thread_arg_t args[NUM_PRODUCERS];
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        ASSERT_EQ(rel4u_thread_create(&state.threads[state.threads_started], consumer_func, &state), 0);
        state.threads_started++;
    }
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        args[i].state = &state;
        args[i].producer = (uint32_t)i;
        ASSERT_EQ(rel4u_thread_create(&state.threads[state.threads_started], producer_func, &args[i]), 0);
        state.threads_started++;
    }
    for (size_t i = 0; i < state.threads_started; i++) rel4u_thread_join(state.threads[i]);
    state.threads_started = 0;
    ASSERT_FALSE(atomic_load(&state.failed));
    ASSERT_EQ(atomic_load(&state.consumed), target);
    for (uint32_t i = 0; i < target; i++) ASSERT_EQ(atomic_load(&seen[i]), 1);
    ASSERT_EQ(rel4u_mpmc_size(&q), 0);
    if (mode == REL4U_MEM_DYNAMIC) ASSERT_EQ(rel4u_mpmc_bytes(&q), 0);
    test_cleanup_remove(&state);
    test_cleanup_remove(seen_allocation);
    free(seen_allocation);
    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_multithreaded_stress(void) {
    return run_mpmc_stress(REL4U_MEM_FIXED);
}

static bool test_mpmc_dynamic_stress(void) {
    return run_mpmc_stress(REL4U_MEM_DYNAMIC);
}

typedef struct wake_waiter_arg {
    rel4u_mpmc_queue_t* q;
    int32_t timeout_ms;
    atomic_bool finished;
    bool pop_result;
} wake_waiter_arg_t;

static void* wake_waiter_func(void* arg) {
    wake_waiter_arg_t* w = (wake_waiter_arg_t*)arg;
    rel4u_mpmc_item_t item;
    w->pop_result = rel4u_mpmc_pop_wait(w->q, &item, w->timeout_ms);
    atomic_store(&w->finished, true);
    return NULL;
}

static bool test_mpmc_pop_wait_timeout_and_push_front(void) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 4, 0), 0);

    rel4u_mpmc_item_t item;
    // 1. Pop with short timeout on empty queue should return false
    ASSERT_FALSE(rel4u_mpmc_pop_wait(&q, &item, 10));
    ASSERT_FALSE(rel4u_mpmc_pop_wait(&q, &item, 0));

    // 2. Wake all must unblock a thread waiting in rel4u_mpmc_pop_wait immediately (both timed and infinite)
    int32_t timeouts_to_test[2] = { 2000, -1 };
    for (int t = 0; t < 2; t++) {
        wake_waiter_arg_t w_arg;
        w_arg.q = &q;
        w_arg.timeout_ms = timeouts_to_test[t];
        atomic_init(&w_arg.finished, false);
        w_arg.pop_result = true;

        rel4u_thread_t waiter_thread;
        ASSERT_EQ(rel4u_thread_create(&waiter_thread, wake_waiter_func, &w_arg), 0);
        // A notification may precede entry to cond_wait. Repeat until the
        // waiter acknowledges it, instead of assuming a sleep schedules it.
        uint64_t deadline = rel4u_time_now_ms() + 1000;
        while (!atomic_load(&w_arg.finished) && rel4u_time_now_ms() < deadline) {
            rel4u_mpmc_wake_all(&q);
            rel4u_time_sleep_ms(1);
        }
        bool woke_on_time = atomic_load(&w_arg.finished);
        if (!woke_on_time) {
            memset(&item, 0, sizeof(item));
            rel4u_mpmc_push_notify(&q, &item);
        }
        rel4u_thread_join(waiter_thread);
        ASSERT_TRUE(woke_on_time);
        ASSERT_FALSE(w_arg.pop_result);
    }

    rel4u_mpmc_destroy(&q);

    // 3. Test push_front in dynamic mode (including full policies)
    size_t node_sz = sizeof(rel4u_mpmc_node_t);
    size_t max_bytes = (node_sz + 4) * 2;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_DYNAMIC, REL4U_QUEUE_FULL_DROP_NEW, 0, max_bytes), 0);

    memset(&item, 0, sizeof(item));
    item.len = 4;
    item.client_id = 100;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));

    item.client_id = 200;
    ASSERT_TRUE(rel4u_mpmc_push_front(&q, &item));

    // Dynamic queue is now at max_bytes; 3rd push_front under DROP_NEW must fail
    item.client_id = 300;
    ASSERT_FALSE(rel4u_mpmc_push_front(&q, &item));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);

    // Pop first item -> should be 200 (pushed to front)
    rel4u_mpmc_item_t popped;
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &popped));
    ASSERT_EQ(popped.client_id, 200);

    // Pop second item -> should be 100
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &popped));
    ASSERT_EQ(popped.client_id, 100);

    ASSERT_EQ(rel4u_mpmc_size(&q), 0);
    ASSERT_EQ(rel4u_mpmc_bytes(&q), 0);
    rel4u_mpmc_destroy(&q);

    // 4. Dynamic mode push_front with DROP_LAST and item exceeding max_bytes
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_DYNAMIC, REL4U_QUEUE_FULL_DROP_LAST, 0, max_bytes), 0);
    item.len = 4;
    item.client_id = 10;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    item.client_id = 20;
    ASSERT_TRUE(rel4u_mpmc_try_push(&q, &item));
    // Queue at max_bytes; push_front(30) should evict oldest (10) and place 30 at head
    item.client_id = 30;
    ASSERT_TRUE(rel4u_mpmc_push_front(&q, &item));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &popped));
    ASSERT_EQ(popped.client_id, 30);
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &popped));
    ASSERT_EQ(popped.client_id, 20);

    // Item whose needed bytes exceed max_bytes must fail immediately
    item.len = 1024;
    ASSERT_FALSE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_FALSE(rel4u_mpmc_push_front(&q, &item));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 3);

    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_push_front_fixed_policies(void) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 3, 0), 0);

    rel4u_mpmc_item_t it;
    memset(&it, 0, sizeof(it));
    it.len = 4;

    for (uint32_t i = 1; i <= 3; i++) {
        it.client_id = i;
        ASSERT_TRUE(rel4u_mpmc_try_push(&q, &it));
    }

    // Queue full under DROP_NEW: push_front must fail and increment dropped_count
    it.client_id = 99;
    ASSERT_FALSE(rel4u_mpmc_push_front(&q, &it));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);
    rel4u_mpmc_destroy(&q);

    // Queue full under DROP_LAST: push_front evicts oldest at head and places new item at front
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_LAST, 3, 0), 0);
    for (uint32_t i = 1; i <= 3; i++) {
        it.client_id = i;
        ASSERT_TRUE(rel4u_mpmc_try_push(&q, &it));
    }

    it.client_id = 99;
    ASSERT_TRUE(rel4u_mpmc_push_front(&q, &it));
    ASSERT_EQ(rel4u_mpmc_dropped_count(&q), 1);
    ASSERT_EQ(rel4u_mpmc_size(&q), 3);

    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &it));
    ASSERT_EQ(it.client_id, 99);
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &it));
    ASSERT_EQ(it.client_id, 2);
    ASSERT_TRUE(rel4u_mpmc_try_pop(&q, &it));
    ASSERT_EQ(it.client_id, 3);

    rel4u_mpmc_destroy(&q);
    return true;
}

static bool test_mpmc_oversize_and_bounds(void) {
    rel4u_mpmc_queue_t q;
    ASSERT_EQ(rel4u_mpmc_init(NULL, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 4, 0), -1);
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 1, 0), -1);
    ASSERT_EQ(rel4u_mpmc_init(&q, REL4U_MEM_FIXED, REL4U_QUEUE_FULL_DROP_NEW, 4, 0), 0);

    rel4u_mpmc_item_t item;
    memset(&item, 0, sizeof(item));
    item.len = REL4U_MAX_MSG_SIZE + 1;

    ASSERT_FALSE(rel4u_mpmc_try_push(&q, &item));
    ASSERT_FALSE(rel4u_mpmc_push_front(&q, &item));
    ASSERT_FALSE(rel4u_mpmc_push_notify(&q, &item));

    ASSERT_FALSE(rel4u_mpmc_try_push(NULL, &item));
    ASSERT_FALSE(rel4u_mpmc_try_pop(NULL, &item));
    ASSERT_FALSE(rel4u_mpmc_push_notify(NULL, &item));
    ASSERT_FALSE(rel4u_mpmc_push_front(NULL, &item));
    ASSERT_FALSE(rel4u_mpmc_pop_wait(NULL, &item, 0));

    rel4u_mpmc_wake_all(NULL);
    ASSERT_EQ(rel4u_mpmc_size(NULL), 0);
    ASSERT_EQ(rel4u_mpmc_bytes(NULL), 0);
    ASSERT_EQ(rel4u_mpmc_dropped_count(NULL), 0);

    rel4u_mpmc_destroy(NULL);
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
    RUN_TEST(test_mpmc_pop_wait_timeout_and_push_front);
    RUN_TEST(test_mpmc_push_front_fixed_policies);
    RUN_TEST(test_mpmc_oversize_and_bounds);
TEST_SUITE_END()
