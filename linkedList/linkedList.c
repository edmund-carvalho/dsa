// SPDX-License-Identifier: MIT
/**
 * @file linked_list.c
 * @brief Opaque implementation of the doubly linked list.
 */

#include "linkedList.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>


/* ------------------------------------------------------------------ */
/* Internal Node Structure                                           */
/* ------------------------------------------------------------------ */
struct ll_node {
    struct ll_node *next;
    struct ll_node *prev;
    uint8_t data[]; /* Flexible array member for clean payload allocation */
};

/* ------------------------------------------------------------------ */
/* Full struct definition – matching public linked_list_t             */
/* ------------------------------------------------------------------ */
struct linked_list_t {
    struct ll_node *head;
    struct ll_node *tail;
    size_t     block_size;
    uint32_t   max_elements;
    uint32_t   occupancy;

    ll_malloc_fn malloc_fn;
    ll_free_fn   free_fn;

    uint32_t           high_watermark;
    uint32_t           low_watermark;
    ll_watermark_cb    high_cb;
    ll_watermark_cb    low_cb;
    void              *user_data;

    uint32_t max_occupancy;
    uint32_t min_occupancy;

    DS_LOCK_T *lock;
};

/* ------------------------------------------------------------------ */
/* Default allocators                                                 */
/* ------------------------------------------------------------------ */
static inline void *ll_default_malloc(size_t size) { return malloc(size); }
static inline void  ll_default_free(void *ptr)      { free(ptr); }

/* ------------------------------------------------------------------ */
/* Watermark updater                                                  */
/* ------------------------------------------------------------------ */
static void ll_update_watermarks(struct linked_list_t *list) {
    if (list->occupancy > list->max_occupancy) list->max_occupancy = list->occupancy;
    if (list->occupancy < list->min_occupancy) list->min_occupancy = list->occupancy;

    if (list->high_cb && list->high_watermark > 0 &&
        list->occupancy >= list->high_watermark)
        list->high_cb(list->user_data, list->occupancy);
    if (list->low_cb && list->low_watermark > 0 &&
        list->occupancy < list->low_watermark)
        list->low_cb(list->user_data, list->occupancy);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
linked_list_t* ll_create(size_t block_size, uint32_t max_elements,
                         ll_malloc_fn malloc_fn, ll_free_fn free_fn
                         , DS_LOCK_T *lock)
{
    if (block_size == 0) return NULL;

    ll_malloc_fn m = malloc_fn ? malloc_fn : ll_default_malloc;
    ll_free_fn   f = free_fn   ? free_fn   : ll_default_free;

    /* Allocate the handle */
    struct linked_list_t *list = (struct linked_list_t*)m(sizeof(*list));
    if (!list) return NULL;

    list->head         = NULL;
    list->tail         = NULL;
    list->block_size   = block_size;
    list->max_elements = max_elements;
    list->occupancy    = 0;

    list->malloc_fn = m;
    list->free_fn = f;

    list->high_watermark = 0;
    list->low_watermark  = 0;
    list->high_cb        = NULL;
    list->low_cb         = NULL;
    list->user_data      = NULL;

    list->max_occupancy = 0;
    list->min_occupancy = (max_elements > 0) ? max_elements : UINT32_MAX;

    list->lock = lock;

    return list;
}

void ll_destroy(linked_list_t *list) {
    if (!list) return;

    /* Free all nodes sequentially */
    struct ll_node *curr = list->head;
    while (curr) {
        struct ll_node *tmp = curr;
        curr = curr->next;
        list->free_fn(tmp);
    }

    /* Free the handle itself */
    ll_free_fn f = list->free_fn ? list->free_fn : ll_default_free;
    f(list);
}

/* ---------- push / pop at ends ---------- */

bool ll_push_back(linked_list_t *list, const void *data) {
    if (!list || !data) return false;
    if (list->max_elements > 0 && list->occupancy >= list->max_elements) return false;

    size_t node_size = sizeof(struct ll_node) + list->block_size;
    struct ll_node *node = (struct ll_node*)list->malloc_fn(node_size);
    if (!node) return false;

    memcpy(node->data, data, list->block_size);
    node->prev = list->tail;
    node->next = NULL;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(list->lock);
#endif

    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node;
    }
    list->tail = node;
    list->occupancy++;
    ll_update_watermarks(list);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(list->lock);
#endif
    return true;
}

bool ll_pop_front(linked_list_t *list, void *data_out) {
    if (!list || !data_out || !list->head) return false;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(list->lock);
#endif

    struct ll_node *node = list->head;
    memcpy(data_out, node->data, list->block_size);
    list->head = node->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }
    list->occupancy--;
    list->free_fn(node);
    ll_update_watermarks(list);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(list->lock);
#endif
    return true;
}

bool ll_push_front(linked_list_t *list, const void *data) {
    if (!list || !data) return false;
    if (list->max_elements > 0 && list->occupancy >= list->max_elements) return false;

    size_t node_size = sizeof(struct ll_node) + list->block_size;
    struct ll_node *node = (struct ll_node*)list->malloc_fn(node_size);
    if (!node) return false;

    memcpy(node->data, data, list->block_size);
    node->prev = NULL;
    node->next = list->head;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(list->lock);
#endif

    if (list->head) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }
    list->head = node;
    list->occupancy++;
    ll_update_watermarks(list);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(list->lock);
#endif
    return true;
}

bool ll_pop_back(linked_list_t *list, void *data_out) {
    if (!list || !data_out || !list->tail) return false;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(list->lock);
#endif

    struct ll_node *node = list->tail;
    memcpy(data_out, node->data, list->block_size);
    list->tail = node->prev;
    if (list->tail) {
        list->tail->next = NULL;
    } else {
        list->head = NULL;
    }
    list->occupancy--;
    list->free_fn(node);
    ll_update_watermarks(list);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(list->lock);
#endif
    return true;
}

bool ll_peek_front(const linked_list_t *list, void *data_out) {
    if (!list || !data_out || !list->head) return false;
    memcpy(data_out, list->head->data, list->block_size);
    return true;
}

bool ll_peek_back(const linked_list_t *list, void *data_out) {
    if (!list || !data_out || !list->tail) return false;
    memcpy(data_out, list->tail->data, list->block_size);
    return true;
}

uint32_t ll_occupancy(const linked_list_t *list) {
    return list ? list->occupancy : 0;
}

bool ll_is_empty(const linked_list_t *list) {
    return list ? (list->head == NULL) : true;
}

bool ll_is_full(const linked_list_t *list) {
    if (!list) return true;
    if (list->max_elements == 0) return false;  /* unlimited */
    return list->occupancy >= list->max_elements;
}

void ll_set_high_watermark(linked_list_t *list, uint32_t threshold,
                           ll_watermark_cb cb, void *user_data) {
    if (list) {
        list->high_watermark = threshold;
        list->high_cb        = cb;
        list->user_data      = user_data;
    }
}

void ll_set_low_watermark(linked_list_t *list, uint32_t threshold,
                          ll_watermark_cb cb, void *user_data) {
    if (list) {
        list->low_watermark = threshold;
        list->low_cb        = cb;
        list->user_data     = user_data;
    }
}

uint32_t ll_get_max_occupancy(const linked_list_t *list) {
    return list ? list->max_occupancy : 0;
}

uint32_t ll_get_min_occupancy(const linked_list_t *list) {
    return list ? list->min_occupancy : 0;
}

void ll_reset_occupancy_stats(linked_list_t *list) {
    if (list) {
        list->max_occupancy = list->occupancy;
        list->min_occupancy = list->occupancy;
    }
}