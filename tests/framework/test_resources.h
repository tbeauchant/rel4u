#ifndef TEST_RESOURCES_H
#define TEST_RESOURCES_H

#include "test_framework.h"
#include "../../include/rel4u.h"
#include "../../src/core/rel4u_mpmc.h"
#include "../../src/core/rel4u_window.h"
#include "../../src/core/rel4u_session.h"
#include "network_sim.h"

static inline void test_cleanup_client(void* object) {
    rel4u_client_destroy((rel4u_client_t*)object);
}
static inline rel4u_client_t* test_client_create(const rel4u_client_config_t* config) {
    rel4u_client_t* object = rel4u_client_create(config);
    test_cleanup_register(object, test_cleanup_client);
    return object;
}
static inline void test_client_destroy(rel4u_client_t* object) {
    test_cleanup_remove(object);
    rel4u_client_destroy(object);
}

static inline void test_cleanup_server(void* object) {
    rel4u_server_destroy((rel4u_server_t*)object);
}
static inline rel4u_server_t* test_server_create(const rel4u_server_config_t* config) {
    rel4u_server_t* object = rel4u_server_create(config);
    test_cleanup_register(object, test_cleanup_server);
    return object;
}
static inline void test_server_destroy(rel4u_server_t* object) {
    test_cleanup_remove(object);
    rel4u_server_destroy(object);
}

static inline void test_cleanup_net_sim(void* object) {
    rel4u_net_sim_destroy((rel4u_net_sim_t*)object);
}
static inline rel4u_net_sim_t* test_net_sim_create(uint16_t proxy_port, uint16_t server_port, const rel4u_net_sim_config_t* config) {
    rel4u_net_sim_t* object = rel4u_net_sim_create(proxy_port, server_port, config);
    test_cleanup_register(object, test_cleanup_net_sim);
    return object;
}
static inline void test_net_sim_destroy(rel4u_net_sim_t* object) {
    test_cleanup_remove(object);
    rel4u_net_sim_destroy(object);
}

static inline void test_cleanup_mpmc(void* object) {
    rel4u_mpmc_destroy((rel4u_mpmc_queue_t*)object);
}
static inline int test_mpmc_init(rel4u_mpmc_queue_t* object, rel4u_memory_mode_t mode, rel4u_queue_full_policy_t policy, uint32_t capacity, size_t max_bytes) {
    int result = rel4u_mpmc_init(object, mode, policy, capacity, max_bytes);
    if (result == 0) test_cleanup_register(object, test_cleanup_mpmc);
    return result;
}
static inline void test_mpmc_destroy(rel4u_mpmc_queue_t* object) {
    test_cleanup_remove(object);
    rel4u_mpmc_destroy(object);
}

static inline void test_cleanup_send_window(void* object) {
    rel4u_send_window_destroy((rel4u_send_window_t*)object);
}
static inline int test_send_window_init(rel4u_send_window_t* object, uint32_t capacity, uint32_t initial_seq) {
    int result = rel4u_send_window_init(object, capacity, initial_seq);
    if (result == 0) test_cleanup_register(object, test_cleanup_send_window);
    return result;
}
static inline void test_send_window_destroy(rel4u_send_window_t* object) {
    test_cleanup_remove(object);
    rel4u_send_window_destroy(object);
}

static inline void test_cleanup_recv_window(void* object) {
    rel4u_recv_window_destroy((rel4u_recv_window_t*)object);
}
static inline int test_recv_window_init(rel4u_recv_window_t* object, uint32_t capacity, uint32_t initial_seq) {
    int result = rel4u_recv_window_init(object, capacity, initial_seq);
    if (result == 0) test_cleanup_register(object, test_cleanup_recv_window);
    return result;
}
static inline void test_recv_window_destroy(rel4u_recv_window_t* object) {
    test_cleanup_remove(object);
    rel4u_recv_window_destroy(object);
}

static inline void test_cleanup_session_table(void* object) {
    rel4u_session_table_destroy((rel4u_session_table_t*)object);
}
static inline int test_session_table_init(rel4u_session_table_t* object, uint32_t max_clients, rel4u_max_clients_policy_t policy, uint32_t window_size) {
    int result = rel4u_session_table_init(object, max_clients, policy, window_size);
    if (result == 0) test_cleanup_register(object, test_cleanup_session_table);
    return result;
}
static inline void test_session_table_destroy(rel4u_session_table_t* object) {
    test_cleanup_remove(object);
    rel4u_session_table_destroy(object);
}

/* Include after the library headers; only test allocations are tracked. */
#define rel4u_client_create test_client_create
#define rel4u_client_destroy test_client_destroy
#define rel4u_server_create test_server_create
#define rel4u_server_destroy test_server_destroy
#define rel4u_net_sim_create test_net_sim_create
#define rel4u_net_sim_destroy test_net_sim_destroy
#define rel4u_mpmc_init test_mpmc_init
#define rel4u_mpmc_destroy test_mpmc_destroy
#define rel4u_send_window_init test_send_window_init
#define rel4u_send_window_destroy test_send_window_destroy
#define rel4u_recv_window_init test_recv_window_init
#define rel4u_recv_window_destroy test_recv_window_destroy
#define rel4u_session_table_init test_session_table_init
#define rel4u_session_table_destroy test_session_table_destroy

#endif
