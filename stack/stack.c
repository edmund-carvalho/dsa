// SPDX-License-Identifier: MIT
/**
 * @file stack.c
 * @brief Opaque implementation of the LIFO stack.
 *
 * Supports dynamic allocation (stack_create) and static buffer usage
 * (stack_create_static). The handle itself is always dynamically allocated;
 * only the data buffer can be user‑supplied.
 */

#include "stack.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Full struct definition – hidden from users                         */
/* ------------------------------------------------------------------ */
struct stack_t {
    uint8_t  *buffer;       /**< Pointer to the data storage.           */
    size_t    block_size;   /**< Size of one element in bytes.          */
    uint32_t  capacity;     /**< Maximum number of elements.            */
    uint32_t  top;          /**< Index of next free slot (= occupancy). */
    bool      is_static;    /**< true → buffer is user‑owned, don't free. */

    stack_malloc_fn malloc_fn;
    stack_free_fn   free_fn;

    uint32_t            high_watermark;
    uint32_t            low_watermark;
    stack_watermark_cb  high_cb;
    stack_watermark_cb  low_cb;
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
static inline void *stack_default_malloc(size_t size) { return malloc(size); }
static inline void  stack_default_free(void *ptr)      { free(ptr); }

/* ------------------------------------------------------------------ */
/*  Watermark updater                                                  */
/* ------------------------------------------------------------------ */
static void stack_update_watermarks(stack_t *st) {
    uint32_t occ = st->top;  /* top equals occupancy */
    if (occ > st->max_occupancy) st->max_occupancy = occ;
    if (occ < st->min_occupancy) st->min_occupancy = occ;

    if (st->high_cb && st->high_watermark > 0 && occ >= st->high_watermark)
        st->high_cb(st->user_data, occ);
    if (st->low_cb && st->low_watermark > 0 && occ < st->low_watermark)
        st->low_cb(st->user_data, occ);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

stack_t* stack_create(size_t block_size, uint32_t capacity,
                      stack_malloc_fn malloc_fn, stack_free_fn free_fn
#ifdef DS_USE_THREAD_SAFETY
                      , DS_LOCK_T *lock
#endif
)
{
    if (block_size == 0 || capacity == 0) return NULL;

    stack_malloc_fn m = malloc_fn ? malloc_fn : stack_default_malloc;
    stack_free_fn   f = free_fn   ? free_fn   : stack_default_free;

    /* Allocate the handle */
    stack_t *st = (stack_t*)m(sizeof(*st));
    if (!st) return NULL;

    /* Allocate the data buffer */
    st->buffer = (uint8_t*)m(block_size * capacity);
    if (!st->buffer) {
        f(st);
        return NULL;
    }

    st->block_size = block_size;
    st->capacity   = capacity;
    st->top        = 0;
    st->is_static  = false;

    st->malloc_fn = m;
    st->free_fn   = f;

    st->high_watermark = 0;
    st->low_watermark  = 0;
    st->high_cb        = NULL;
    st->low_cb         = NULL;
    st->user_data      = NULL;

    st->max_occupancy = 0;
    st->min_occupancy = capacity;   /* start high so first drop is detected */

#ifdef DS_USE_THREAD_SAFETY
    st->lock = lock;
#endif
    return st;
}

stack_t* stack_create_static(uint8_t *buffer,
                             size_t block_size, uint32_t capacity
#ifdef DS_USE_THREAD_SAFETY
                             , DS_LOCK_T *lock
#endif
) {
    if (!buffer || block_size == 0 || capacity == 0) return NULL;

    /* Use default allocator for the handle only; user buffer for data */
    stack_t *st = (stack_t*)stack_default_malloc(sizeof(*st));
    if (!st) return NULL;

    st->buffer      = buffer;
    st->block_size  = block_size;
    st->capacity    = capacity;
    st->top         = 0;
    st->is_static   = true;

    st->malloc_fn = NULL;   /* not used for buffer management */
    st->free_fn   = NULL;

    st->high_watermark = 0;
    st->low_watermark  = 0;
    st->high_cb        = NULL;
    st->low_cb         = NULL;
    st->user_data      = NULL;

    st->max_occupancy = 0;
    st->min_occupancy = capacity;

#ifdef DS_USE_THREAD_SAFETY
    st->lock = lock;
#endif
    return st;
}

void stack_destroy(stack_t *st) {
    if (!st) return;

    /* Free the data buffer only if we own it */
    if (!st->is_static && st->buffer && st->free_fn) {
        st->free_fn(st->buffer);
    }

    /* Free the handle itself */
    stack_free_fn f = st->free_fn ? st->free_fn : stack_default_free;
    f(st);
}

bool stack_push(stack_t *st, const void *data) {
    if (!st || !data) return false;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(st->lock);
#endif

    /* Check fullness safely inside the critical section using the pointer */
    if (st->top >= st->capacity) {
#ifdef DS_USE_THREAD_SAFETY
        DS_UNLOCK(st->lock);
#endif
        return false;
    }

    memcpy(st->buffer + (st->top * st->block_size), data, st->block_size);
    st->top++;
    stack_update_watermarks(st);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(st->lock);
#endif
    return true;
}

bool stack_pop(stack_t *st, void *data_out) {
    if (!st || !data_out) return false;

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(st->lock);
#endif

    /* Check emptiness safely inside the critical section using the pointer */
    if (st->top == 0) {
#ifdef DS_USE_THREAD_SAFETY
        DS_UNLOCK(st->lock);
#endif
        return false;
    }

    st->top--;
    memcpy(data_out, st->buffer + (st->top * st->block_size), st->block_size);
    stack_update_watermarks(st);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(st->lock);
#endif
    return true;
}

bool stack_peek(const stack_t *st, void *data_out) {
    if (!st || !data_out || stack_is_empty(st)) return false;
    memcpy(data_out, st->buffer + ((st->top - 1) * st->block_size), st->block_size);
    return true;
}

uint32_t stack_occupancy(const stack_t *st) {
    return st ? st->top : 0;
}

bool stack_is_empty(const stack_t *st) {
    return st ? (st->top == 0) : true;
}

bool stack_is_full(const stack_t *st) {
    return st ? (st->top >= st->capacity) : false;
}

void stack_set_high_watermark(stack_t *st, uint32_t threshold,
                              stack_watermark_cb cb, void *user_data) {
    if (st) {
        st->high_watermark = threshold;
        st->high_cb        = cb;
        st->user_data      = user_data;
    }
}

void stack_set_low_watermark(stack_t *st, uint32_t threshold,
                             stack_watermark_cb cb, void *user_data) {
    if (st) {
        st->low_watermark = threshold;
        st->low_cb        = cb;
        st->user_data     = user_data;
    }
}

uint32_t stack_get_max_occupancy(const stack_t *st) {
    return st ? st->max_occupancy : 0;
}

uint32_t stack_get_min_occupancy(const stack_t *st) {
    return st ? st->min_occupancy : 0;
}

void stack_reset_occupancy_stats(stack_t *st) {
    if (st) {
        st->max_occupancy = st->top;
        st->min_occupancy = st->top;
    }
}