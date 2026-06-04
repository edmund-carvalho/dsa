// SPDX-License-Identifier: MIT
/**
 * @file circular_buffer.c
 * @brief Opaque implementation of the circular buffer (FIFO).
 *
 * The circular buffer stores fixed‑size blocks in a contiguous array.
 * Data is copied in/out by value using memcpy().
 *
 * Two creation paths are provided:
 *  - cb_create()        - dynamic allocation of both handle and data buffer.
 *  - cb_create_static() - uses a user‑provided data buffer; only the handle
 *                          is allocated dynamically (with default_malloc).
 *
 * When DS_USE_THREAD_SAFETY is defined, all mutating operations are
 * wrapped with user‑supplied lock/unlock primitives.
 */

#include "circularBuffer.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Full struct definition - hidden from users                         */
/* ------------------------------------------------------------------ */
struct circular_buffer_t {
    uint8_t  *buffer;          /**< Pointer to the underlying data storage.     */
    size_t    block_size;      /**< Size in bytes of one element.               */
    uint32_t  block_count;     /**< Maximum number of blocks.                   */
    uint32_t  head;            /**< Index of the oldest element (next to pop).  */
    uint32_t  tail;            /**< Index where the next element will be pushed.*/
    uint32_t  occupancy;       /**< Current number of stored elements.          */
    bool      full;            /**< True when occupancy == block_count.         */
    bool      is_static;       /**< True → data buffer is user‑owned; don't free. */

    cb_malloc_fn malloc_fn;    /**< Custom allocator for dynamic path.          */
    cb_free_fn   free_fn;      /**< Custom deallocator for dynamic path.        */

    uint32_t        high_watermark;  /**< Threshold for high‑occupancy callback.   */
    uint32_t        low_watermark;   /**< Threshold for low‑occupancy callback.    */
    cb_watermark_cb high_cb;         /**< Callback when occupancy >= high_watermark.*/
    cb_watermark_cb low_cb;          /**< Callback when occupancy <  low_watermark. */
    void           *cb_user_data;    /**< User data passed to watermark callbacks.  */

    uint32_t max_occupancy;          /**< Highest observed occupancy since reset.   */
    uint32_t min_occupancy;          /**< Lowest observed occupancy since reset.    */

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK_T *lock;                 /**< Pointer to user‑provided lock instance.   */
#endif
};

/* ------------------------------------------------------------------ */
/*  Default allocators (used when user passes NULL)                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Fallback malloc - wraps the standard library malloc().
 */
static inline void* cb_default_malloc(size_t size)
{
    return malloc(size);
}

/**
 * @brief Fallback free - wraps the standard library free().
 */
static inline void cb_default_free(void *ptr)
{
    free(ptr);
}

/* ------------------------------------------------------------------ */
/*  Internal helper: update statistics and fire watermark callbacks    */
/* ------------------------------------------------------------------ */

/**
 * @brief Update occupancy statistics and invoke watermark callbacks if
 *        the current occupancy crosses the configured thresholds.
 *
 * Called after every push and pop.
 *
 * @param cb Pointer to the circular buffer instance.
 */
static void cb_update_watermarks(circular_buffer_t *cb)
{
    /* Track maximum and minimum occupancy */
    if (cb->occupancy > cb->max_occupancy) {
        cb->max_occupancy = cb->occupancy;
    }
    if (cb->occupancy < cb->min_occupancy) {
        cb->min_occupancy = cb->occupancy;
    }

    /* High watermark: fire when occupancy reaches or exceeds threshold */
    if (cb->high_cb != NULL &&
        cb->high_watermark > 0 &&
        cb->occupancy >= cb->high_watermark) {
        cb->high_cb(cb->cb_user_data, cb->occupancy);
    }

    /* Low watermark: fire when occupancy drops below threshold */
    if (cb->low_cb != NULL &&
        cb->low_watermark > 0 &&
        cb->occupancy < cb->low_watermark) {
        cb->low_cb(cb->cb_user_data, cb->occupancy);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API - Lifetime                                               */
/* ------------------------------------------------------------------ */

circular_buffer_t* cb_create(size_t block_size,
                             uint32_t block_count,
                             cb_malloc_fn malloc_fn,
                             cb_free_fn free_fn
#ifdef DS_USE_THREAD_SAFETY
                             ,
                             DS_LOCK_T *lock
#endif
)
{
    if (block_size == 0 || block_count == 0) {
        return NULL;
    }

    /* Choose allocators (NULL → standard library) */
    cb_malloc_fn m = (malloc_fn != NULL) ? malloc_fn : cb_default_malloc;
    cb_free_fn   f = (free_fn   != NULL) ? free_fn   : cb_default_free;

    /* Allocate the handle itself */
    circular_buffer_t *cb = (circular_buffer_t*) m(sizeof(*cb));
    if (cb == NULL) {
        return NULL;
    }

    /* Allocate the data buffer */
    cb->buffer = (uint8_t*) m(block_size * block_count);
    if (cb->buffer == NULL) {
        f(cb);   /* clean up the handle on failure */
        return NULL;
    }

    /* Populate metadata */
    cb->block_size  = block_size;
    cb->block_count = block_count;
    cb->head        = 0;
    cb->tail        = 0;
    cb->occupancy   = 0;
    cb->full        = false;
    cb->is_static   = false;

    cb->malloc_fn = m;
    cb->free_fn   = f;

    /* Watermarks disabled by default */
    cb->high_watermark = 0;
    cb->low_watermark  = 0;
    cb->high_cb        = NULL;
    cb->low_cb         = NULL;
    cb->cb_user_data   = NULL;

    /* Statistics: max starts at 0, min starts at full capacity so the
       first drop is correctly recorded */
    cb->max_occupancy = 0;
    cb->min_occupancy = block_count;

#ifdef DS_USE_THREAD_SAFETY
    cb->lock = lock;
#endif

    return cb;
}

circular_buffer_t* cb_create_static(uint8_t *buffer,
                                    size_t block_size,
                                    uint32_t block_count
#ifdef DS_USE_THREAD_SAFETY
                                    ,
                                    DS_LOCK_T *lock
#endif
)
{
    if (buffer == NULL || block_size == 0 || block_count == 0) {
        return NULL;
    }

    /* Allocate only the handle (metadata) with the default allocator.
       The data buffer is supplied by the user and will not be freed. */
    circular_buffer_t *cb = (circular_buffer_t*) cb_default_malloc(sizeof(*cb));
    if (cb == NULL) {
        return NULL;
    }

    cb->buffer      = buffer;
    cb->block_size  = block_size;
    cb->block_count = block_count;
    cb->head        = 0;
    cb->tail        = 0;
    cb->occupancy   = 0;
    cb->full        = false;
    cb->is_static   = true;   /* marks that buffer is user‑owned */

    /* Allocators are not used for the data path; they remain NULL.
       cb_destroy() will fall back to cb_default_free for the handle. */
    cb->malloc_fn = NULL;
    cb->free_fn   = NULL;

    cb->high_watermark = 0;
    cb->low_watermark  = 0;
    cb->high_cb        = NULL;
    cb->low_cb         = NULL;
    cb->cb_user_data   = NULL;

    cb->max_occupancy = 0;
    cb->min_occupancy = block_count;

#ifdef DS_USE_THREAD_SAFETY
    cb->lock = lock;
#endif

    return cb;
}

void cb_destroy(circular_buffer_t *cb)
{
    if (cb == NULL) {
        return;
    }

    /* Free the data buffer only if we own it (dynamic allocation path).
       For cb_create_static, is_static is true and we skip this. */
    if (!cb->is_static && cb->buffer != NULL && cb->free_fn != NULL) {
        cb->free_fn(cb->buffer);
    }

    /* Free the handle itself.
       - Dynamic path: uses the user‑supplied free_fn.
       - Static path:  free_fn is NULL → falls back to cb_default_free,
         which correctly matches the cb_default_malloc used to allocate
         the handle in cb_create_static(). */
    cb_free_fn f = (cb->free_fn != NULL) ? cb->free_fn : cb_default_free;
    f(cb);
}

/* ------------------------------------------------------------------ */
/*  Public API - Core Operations                                       */
/* ------------------------------------------------------------------ */

bool cb_push(circular_buffer_t *cb, const void *data)
{
    if (cb == NULL || data == NULL) {
        return false;
    }

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(cb->lock);
#endif

    /* Re-evaluate boundary constraint safely within the locked scope */
    if (cb->full) {
#ifdef DS_USE_THREAD_SAFETY
        DS_UNLOCK(cb->lock);
#endif
        return false;
    }

    /* Copy user data into the buffer at the tail position */
    memcpy(cb->buffer + (cb->tail * cb->block_size), data, cb->block_size);
    cb->tail = (cb->tail + 1) % cb->block_count;

    cb->occupancy++;
    if (cb->occupancy == cb->block_count) {
        cb->full = true;
    }

    cb_update_watermarks(cb);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(cb->lock);
#endif
    return true;
}

bool cb_pop(circular_buffer_t *cb, void *data_out)
{
    if (cb == NULL || data_out == NULL) {
        return false;
    }

#ifdef DS_USE_THREAD_SAFETY
    DS_LOCK(cb->lock);
#endif

    /* Re-evaluate boundary constraint safely within the locked scope */
    if (cb->occupancy == 0) {
#ifdef DS_USE_THREAD_SAFETY
        DS_UNLOCK(cb->lock);
#endif
        return false;
    }

    /* Copy oldest element out, then advance head */
    memcpy(data_out, cb->buffer + (cb->head * cb->block_size), cb->block_size);
    cb->head = (cb->head + 1) % cb->block_count;

    /* Buffer is no longer full after a successful pop */
    cb->full = false;
    if (cb->occupancy > 0) {
        cb->occupancy--;
    }

    cb_update_watermarks(cb);

#ifdef DS_USE_THREAD_SAFETY
    DS_UNLOCK(cb->lock);
#endif
    return true;
}

bool cb_peek(const circular_buffer_t *cb, void *data_out)
{
    if (cb == NULL || data_out == NULL || cb_is_empty(cb)) {
        return false;
    }

    /* Peek is a read‑only operation — no locking strictly required,
       but it returns a consistent snapshot because it's a single memcpy. */
    memcpy(data_out, cb->buffer + (cb->head * cb->block_size), cb->block_size);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Public API - State Queries                                         */
/* ------------------------------------------------------------------ */

uint32_t cb_occupancy(const circular_buffer_t *cb)
{
    return (cb != NULL) ? cb->occupancy : 0;
}

bool cb_is_empty(const circular_buffer_t *cb)
{
    return (cb != NULL) ? (cb->occupancy == 0) : true;
}

bool cb_is_full(const circular_buffer_t *cb)
{
    return (cb != NULL) ? cb->full : false;
}

/* ------------------------------------------------------------------ */
/*  Public API - Watermark Configuration                               */
/* ------------------------------------------------------------------ */

void cb_set_high_watermark(circular_buffer_t *cb,
                           uint32_t threshold,
                           cb_watermark_cb cb_fn,
                           void *user_data)
{
    if (cb != NULL) {
        cb->high_watermark = threshold;
        cb->high_cb        = cb_fn;
        cb->cb_user_data   = user_data;
    }
}

void cb_set_low_watermark(circular_buffer_t *cb,
                          uint32_t threshold,
                          cb_watermark_cb cb_fn,
                          void *user_data)
{
    if (cb != NULL) {
        cb->low_watermark = threshold;
        cb->low_cb        = cb_fn;
        cb->cb_user_data  = user_data;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API - Occupancy Statistics                                  */
/* ------------------------------------------------------------------ */

uint32_t cb_get_max_occupancy(const circular_buffer_t *cb)
{
    return (cb != NULL) ? cb->max_occupancy : 0;
}

uint32_t cb_get_min_occupancy(const circular_buffer_t *cb)
{
    return (cb != NULL) ? cb->min_occupancy : 0;
}

void cb_reset_occupancy_stats(circular_buffer_t *cb)
{
    if (cb != NULL) {
        cb->max_occupancy = cb->occupancy;
        cb->min_occupancy = cb->occupancy;
    }
}