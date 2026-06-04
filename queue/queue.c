// SPDX-License-Identifier: MIT
/**
 * @file queue.c
 * @brief Opaque implementation of the linked‑list FIFO queue.
 */

#include "queue.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/*  Internal Node Structure                                           */
/* ------------------------------------------------------------------ */
typedef struct queue_node_t {
    struct queue_node_t *next;
    uint8_t data[]; /* Flexible array member for payload data */
} queue_node_t;

/* ------------------------------------------------------------------ */
/*  Full struct definition – hidden from users                         */
/* ------------------------------------------------------------------ */
struct queue_t {
    queue_node_t *head;
    queue_node_t *tail;
    size_t        block_size;
    uint32_t      max_nodes;
    uint32_t      occupancy;

    queue_malloc_fn malloc_fn;
    queue_free_fn   free_fn;

    uint32_t            high_watermark;
    uint32_t            low_watermark;
    queue_watermark_cb  high_cb;
    queue_watermark_cb  low_cb;
    void               *user_data;

    uint32_t max_occupancy;
    uint32_t min_occupancy;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK_T *lock;
#endif
};

/* ------------------------------------------------------------------ */
/*  Default allocators                                                 */
/* ------------------------------------------------------------------ */
static inline void *queue_default_malloc(size_t size) { return malloc(size); }
static inline void  queue_default_free(void *ptr)      { free(ptr); }

/* ------------------------------------------------------------------ */
/*  Watermark updater                                                  */
/* ------------------------------------------------------------------ */
static void queue_update_watermarks(queue_t *q) {
    if (q->occupancy > q->max_occupancy) q->max_occupancy = q->occupancy;
    if (q->occupancy < q->min_occupancy) q->min_occupancy = q->occupancy;

    if (q->high_cb && q->high_watermark > 0 && q->occupancy >= q->high_watermark)
        q->high_cb(q->user_data, q->occupancy);
    if (q->low_cb && q->low_watermark > 0 && q->occupancy < q->low_watermark)
        q->low_cb(q->user_data, q->occupancy);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

queue_t* queue_create(size_t block_size, uint32_t max_nodes,
                      queue_malloc_fn malloc_fn, queue_free_fn free_fn
#ifdef DS_USE_THREAD_SAFETY
                      , DS_LOCK_T *lock
#endif
) {
    if (block_size == 0) return NULL;

    queue_malloc_fn m = malloc_fn ? malloc_fn : queue_default_malloc;
    queue_free_fn   f = free_fn   ? free_fn   : queue_default_free;

    /* Allocate the handle */
    queue_t *q = (queue_t*)m(sizeof(*q));
    if (!q) return NULL;

    q->head       = NULL;
    q->tail       = NULL;
    q->block_size = block_size;
    q->max_nodes  = max_nodes;
    q->occupancy  = 0;

    q->malloc_fn = m;
    q->free_fn   = f;

    q->high_watermark = 0;
    q->low_watermark  = 0;
    q->high_cb        = NULL;
    q->low_cb         = NULL;
    q->user_data      = NULL;

    q->max_occupancy = 0;
    q->min_occupancy = (max_nodes > 0) ? max_nodes : UINT32_MAX;

#ifdef DS_USE_THREAD_SAFETY
    q->lock = lock;
#endif
    return q;
}

void queue_destroy(queue_t *q) {
    if (!q) return;

    /* Free all nodes */
    queue_node_t *curr = q->head;
    while (curr) {
        queue_node_t *tmp = curr;
        curr = curr->next;
        q->free_fn(tmp);
    }

    /* Free the handle itself */
    q->free_fn(q);
}

bool queue_enqueue(queue_t *q, const void *data) {
    if (!q || !data) return false;

    /* Allocate new node outside lock to minimize critical section */
    size_t node_size = sizeof(queue_node_t) + q->block_size;
    queue_node_t *node = (queue_node_t*)q->malloc_fn(node_size);
    if (!node) return false;

    memcpy(node->data, data, q->block_size);
    node->next = NULL;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(q->lock);
#endif

    /* Re-check boundary condition safely inside the lock */
    if (q->max_nodes > 0 && q->occupancy >= q->max_nodes) {
#ifdef DS_USE_THREAD_SAFETY
        DS_UNLOCK(q->lock);
#endif
        q->free_fn(node);
        return false;
    }

    if (q->tail) {
        q->tail->next = node;
    } else {
        q->head = node;
    }
    q->tail = node;
    q->occupancy++;
    queue_update_watermarks(q);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(q->lock);
#endif
    return true;
}

bool queue_dequeue(queue_t *q, void *data_out) {
    if (!q || !data_out) return false;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(q->lock);
#endif

    if (!q->head) {
#ifdef DS_USE_THREAD_SAFETY
        DS_UNLOCK(q->lock);
#endif
        return false;
    }

    queue_node_t *node = q->head;
    memcpy(data_out, node->data, q->block_size);
    q->head = node->next;
    if (!q->head) q->tail = NULL;

    q->occupancy--;
    queue_update_watermarks(q);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(q->lock);
#endif

    /* Free the node outside the lock to reduce contention */
    q->free_fn(node);
    return true;
}

bool queue_peek(const queue_t *q, void *data_out) {
    if (!q || !data_out || !q->head) return false;
    memcpy(data_out, q->head->data, q->block_size);
    return true;
}

uint32_t queue_occupancy(const queue_t *q) {
    return q ? q->occupancy : 0;
}

bool queue_is_empty(const queue_t *q) {
    return q ? (q->head == NULL) : true;
}

bool queue_is_full(const queue_t *q) {
    if (!q) return true;
    if (q->max_nodes == 0) return false;  /* unlimited */
    return q->occupancy >= q->max_nodes;
}

void queue_set_high_watermark(queue_t *q, uint32_t threshold,
                              queue_watermark_cb cb, void *user_data) {
    if (q) {
        q->high_watermark = threshold;
        q->high_cb        = cb;
        q->user_data      = user_data;
    }
}

void queue_set_low_watermark(queue_t *q, uint32_t threshold,
                             queue_watermark_cb cb, void *user_data) {
    if (q) {
        q->low_watermark = threshold;
        q->low_cb        = cb;
        q->user_data     = user_data;
    }
}

uint32_t queue_get_max_occupancy(const queue_t *q) {
    return q ? q->max_occupancy : 0;
}

uint32_t queue_get_min_occupancy(const queue_t *q) {
    return q ? q->min_occupancy : 0;
}

void queue_reset_occupancy_stats(queue_t *q) {
    if (q) {
        q->max_occupancy = q->occupancy;
        q->min_occupancy = q->occupancy;
    }
}