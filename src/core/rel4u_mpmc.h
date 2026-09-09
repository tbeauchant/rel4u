#ifndef REL4U_MPMC_H
#define REL4U_MPMC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rel4u.h"
#include "../platform/rel4u_threads.h"

#define REL4U_MAX_MSG_SIZE 2048

typedef struct rel4u_mpmc_item {
    uint32_t client_id;
    uint8_t  mode;
    uint16_t len;
    uint8_t  data[REL4U_MAX_MSG_SIZE];
} rel4u_mpmc_item_t;

typedef struct rel4u_mpmc_node {
    uint32_t                 client_id;
    uint8_t                  mode;
    uint16_t                 len;
    size_t                   allocated_size;
    struct rel4u_mpmc_node*  prev;
    struct rel4u_mpmc_node*  next;
    uint8_t                  data[];
} rel4u_mpmc_node_t;

typedef struct rel4u_mpmc_queue {
    rel4u_memory_mode_t        mode;
    rel4u_queue_full_policy_t  full_policy;
    size_t                     max_bytes;       /* Dynamic mode byte limit */
    size_t                     current_bytes;   /* Current heap / buffer bytes */
    uint64_t                   dropped_count;   /* Total items dropped or evicted */

    /* Fixed mode ring buffer */
    rel4u_mpmc_item_t*         items;
    size_t                     capacity;
    size_t                     head;
    size_t                     tail;

    /* Dynamic mode linked list */
    rel4u_mpmc_node_t*         node_head;
    rel4u_mpmc_node_t*         node_tail;

    size_t                     count;
    rel4u_mutex_t              mutex;
    rel4u_cond_t               cond;
} rel4u_mpmc_queue_t;

int  rel4u_mpmc_init(rel4u_mpmc_queue_t* q,
                     rel4u_memory_mode_t mode,
                     rel4u_queue_full_policy_t full_policy,
                     size_t capacity,
                     size_t max_bytes);
void rel4u_mpmc_destroy(rel4u_mpmc_queue_t* q);

bool rel4u_mpmc_try_push(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item);
bool rel4u_mpmc_try_pop(rel4u_mpmc_queue_t* q, rel4u_mpmc_item_t* out_item);

bool rel4u_mpmc_push_notify(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item);
bool rel4u_mpmc_push_front(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item);
bool rel4u_mpmc_pop_wait(rel4u_mpmc_queue_t* q, rel4u_mpmc_item_t* out_item, int32_t timeout_ms);

void rel4u_mpmc_wake_all(rel4u_mpmc_queue_t* q);
size_t rel4u_mpmc_size(const rel4u_mpmc_queue_t* q);
size_t rel4u_mpmc_bytes(rel4u_mpmc_queue_t* q);
uint64_t rel4u_mpmc_dropped_count(rel4u_mpmc_queue_t* q);

#endif /* REL4U_MPMC_H */
