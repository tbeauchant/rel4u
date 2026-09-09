#include "rel4u_mpmc.h"
#include <stdlib.h>
#include <string.h>

int rel4u_mpmc_init(rel4u_mpmc_queue_t* q,
                     rel4u_memory_mode_t mode,
                     rel4u_queue_full_policy_t full_policy,
                     size_t capacity,
                     size_t max_bytes) {
    if (!q) return -1;

    memset(q, 0, sizeof(*q));
    q->mode = mode;
    q->full_policy = full_policy;
    q->dropped_count = 0;
    q->count = 0;

    if (mode == REL4U_MEM_FIXED) {
        if (capacity < 2) return -1;
        q->capacity = capacity;
        q->items = (rel4u_mpmc_item_t*)malloc(sizeof(rel4u_mpmc_item_t) * capacity);
        if (!q->items) return -1;
        q->max_bytes = sizeof(rel4u_mpmc_item_t) * capacity;
        q->current_bytes = q->max_bytes;
        q->head = 0;
        q->tail = 0;
    } else {
        if (max_bytes == 0) max_bytes = 1024 * 1024; /* 1MB default */
        q->max_bytes = max_bytes;
        q->current_bytes = 0;
        q->node_head = NULL;
        q->node_tail = NULL;
    }

    rel4u_mutex_init(&q->mutex);
    rel4u_cond_init(&q->cond);

    return 0;
}

void rel4u_mpmc_destroy(rel4u_mpmc_queue_t* q) {
    if (!q) return;

    rel4u_mutex_lock(&q->mutex);
    rel4u_cond_broadcast(&q->cond);

    if (q->mode == REL4U_MEM_DYNAMIC) {
        rel4u_mpmc_node_t* cur = q->node_head;
        while (cur) {
            rel4u_mpmc_node_t* next = cur->next;
            free(cur);
            cur = next;
        }
        q->node_head = NULL;
        q->node_tail = NULL;
    } else {
        if (q->items) {
            free(q->items);
            q->items = NULL;
        }
    }
    q->capacity = 0;
    q->count = 0;
    q->current_bytes = 0;
    rel4u_mutex_unlock(&q->mutex);

    rel4u_mutex_destroy(&q->mutex);
    rel4u_cond_destroy(&q->cond);
}

static bool mpmc_push_back_locked(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item) {
    if (item->len > REL4U_MAX_MSG_SIZE) return false;

    if (q->mode == REL4U_MEM_FIXED) {
        if (q->count >= q->capacity) {
            if (q->full_policy == REL4U_QUEUE_FULL_DROP_LAST) {
                /* Evict oldest queued message at head */
                q->head = (q->head + 1) % q->capacity;
                q->count--;
                q->dropped_count++;
            } else {
                q->dropped_count++;
                return false;
            }
        }

        q->items[q->tail] = *item;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        return true;
    } else {
        /* Dynamic mode */
        size_t needed = sizeof(rel4u_mpmc_node_t) + item->len;
        if (needed > q->max_bytes) {
            q->dropped_count++;
            return false;
        }

        while (q->current_bytes + needed > q->max_bytes) {
            if (q->full_policy == REL4U_QUEUE_FULL_DROP_LAST && q->node_head != NULL) {
                rel4u_mpmc_node_t* victim = q->node_head;
                q->node_head = victim->next;
                if (q->node_head) {
                    q->node_head->prev = NULL;
                } else {
                    q->node_tail = NULL;
                }
                q->current_bytes -= victim->allocated_size;
                q->count--;
                q->dropped_count++;
                free(victim);
            } else {
                q->dropped_count++;
                return false;
            }
        }

        rel4u_mpmc_node_t* node = (rel4u_mpmc_node_t*)malloc(needed);
        if (!node) {
            q->dropped_count++;
            return false;
        }

        node->client_id = item->client_id;
        node->mode = item->mode;
        node->len = item->len;
        node->allocated_size = needed;
        if (item->len > 0) {
            memcpy(node->data, item->data, item->len);
        }
        node->next = NULL;
        node->prev = q->node_tail;

        if (q->node_tail) {
            q->node_tail->next = node;
        } else {
            q->node_head = node;
        }
        q->node_tail = node;

        q->count++;
        q->current_bytes += needed;
        return true;
    }
}

static bool mpmc_push_front_locked(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item) {
    if (item->len > REL4U_MAX_MSG_SIZE) return false;

    if (q->mode == REL4U_MEM_FIXED) {
        if (q->count >= q->capacity) {
            if (q->full_policy == REL4U_QUEUE_FULL_DROP_LAST) {
                /* Evict oldest queued message at head */
                q->head = (q->head + 1) % q->capacity;
                q->count--;
                q->dropped_count++;
            } else {
                q->dropped_count++;
                return false;
            }
        }

        q->head = (q->head == 0) ? (q->capacity - 1) : (q->head - 1);
        q->items[q->head] = *item;
        q->count++;
        return true;
    } else {
        /* Dynamic mode */
        size_t needed = sizeof(rel4u_mpmc_node_t) + item->len;
        if (needed > q->max_bytes) {
            q->dropped_count++;
            return false;
        }

        while (q->current_bytes + needed > q->max_bytes) {
            if (q->full_policy == REL4U_QUEUE_FULL_DROP_LAST && q->node_head != NULL) {
                rel4u_mpmc_node_t* victim = q->node_head;
                q->node_head = victim->next;
                if (q->node_head) {
                    q->node_head->prev = NULL;
                } else {
                    q->node_tail = NULL;
                }
                q->current_bytes -= victim->allocated_size;
                q->count--;
                q->dropped_count++;
                free(victim);
            } else {
                q->dropped_count++;
                return false;
            }
        }

        rel4u_mpmc_node_t* node = (rel4u_mpmc_node_t*)malloc(needed);
        if (!node) {
            q->dropped_count++;
            return false;
        }

        node->client_id = item->client_id;
        node->mode = item->mode;
        node->len = item->len;
        node->allocated_size = needed;
        if (item->len > 0) {
            memcpy(node->data, item->data, item->len);
        }
        node->prev = NULL;
        node->next = q->node_head;

        if (q->node_head) {
            q->node_head->prev = node;
        } else {
            q->node_tail = node;
        }
        q->node_head = node;

        q->count++;
        q->current_bytes += needed;
        return true;
    }
}

static bool mpmc_pop_head_locked(rel4u_mpmc_queue_t* q, rel4u_mpmc_item_t* out_item) {
    if (q->count == 0) return false;

    if (q->mode == REL4U_MEM_FIXED) {
        *out_item = q->items[q->head];
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        return true;
    } else {
        rel4u_mpmc_node_t* node = q->node_head;
        if (!node) return false;

        q->node_head = node->next;
        if (q->node_head) {
            q->node_head->prev = NULL;
        } else {
            q->node_tail = NULL;
        }
        q->count--;
        q->current_bytes -= node->allocated_size;

        out_item->client_id = node->client_id;
        out_item->mode = node->mode;
        out_item->len = node->len;
        if (node->len > 0) {
            memcpy(out_item->data, node->data, node->len);
        }
        free(node);
        return true;
    }
}

bool rel4u_mpmc_try_push(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item) {
    if (!q || !item) return false;

    rel4u_mutex_lock(&q->mutex);
    bool ok = mpmc_push_back_locked(q, item);
    rel4u_mutex_unlock(&q->mutex);
    return ok;
}

bool rel4u_mpmc_try_pop(rel4u_mpmc_queue_t* q, rel4u_mpmc_item_t* out_item) {
    if (!q || !out_item) return false;

    rel4u_mutex_lock(&q->mutex);
    bool ok = mpmc_pop_head_locked(q, out_item);
    rel4u_mutex_unlock(&q->mutex);
    return ok;
}

bool rel4u_mpmc_push_notify(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item) {
    if (!q || !item) return false;

    rel4u_mutex_lock(&q->mutex);
    bool ok = mpmc_push_back_locked(q, item);
    if (ok) {
        rel4u_cond_signal(&q->cond);
    }
    rel4u_mutex_unlock(&q->mutex);
    return ok;
}

bool rel4u_mpmc_push_front(rel4u_mpmc_queue_t* q, const rel4u_mpmc_item_t* item) {
    if (!q || !item) return false;

    rel4u_mutex_lock(&q->mutex);
    bool ok = mpmc_push_front_locked(q, item);
    if (ok) {
        rel4u_cond_signal(&q->cond);
    }
    rel4u_mutex_unlock(&q->mutex);
    return ok;
}

bool rel4u_mpmc_pop_wait(rel4u_mpmc_queue_t* q, rel4u_mpmc_item_t* out_item, int32_t timeout_ms) {
    if (!q || !out_item) return false;

    rel4u_mutex_lock(&q->mutex);

    while (q->count == 0) {
        if (timeout_ms == 0) {
            rel4u_mutex_unlock(&q->mutex);
            return false;
        } else if (timeout_ms < 0) {
            rel4u_cond_wait(&q->cond, &q->mutex);
        } else {
            bool signaled = rel4u_cond_timedwait_ms(&q->cond, &q->mutex, (uint32_t)timeout_ms);
            if (!signaled && q->count == 0) {
                rel4u_mutex_unlock(&q->mutex);
                return false;
            }
        }
    }

    bool ok = mpmc_pop_head_locked(q, out_item);
    rel4u_mutex_unlock(&q->mutex);
    return ok;
}

void rel4u_mpmc_wake_all(rel4u_mpmc_queue_t* q) {
    if (q) {
        rel4u_mutex_lock(&q->mutex);
        rel4u_cond_broadcast(&q->cond);
        rel4u_mutex_unlock(&q->mutex);
    }
}

size_t rel4u_mpmc_size(const rel4u_mpmc_queue_t* q) {
    if (!q) return 0;
    rel4u_mutex_lock((rel4u_mutex_t*)&q->mutex);
    size_t sz = q->count;
    rel4u_mutex_unlock((rel4u_mutex_t*)&q->mutex);
    return sz;
}

size_t rel4u_mpmc_bytes(rel4u_mpmc_queue_t* q) {
    if (!q) return 0;
    rel4u_mutex_lock(&q->mutex);
    size_t b = q->current_bytes;
    rel4u_mutex_unlock(&q->mutex);
    return b;
}

uint64_t rel4u_mpmc_dropped_count(rel4u_mpmc_queue_t* q) {
    if (!q) return 0;
    rel4u_mutex_lock(&q->mutex);
    uint64_t d = q->dropped_count;
    rel4u_mutex_unlock(&q->mutex);
    return d;
}
