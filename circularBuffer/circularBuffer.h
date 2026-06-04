// SPDX-License-Identifier: MIT
/**
 * @file circularBuffer.h
 * @brief Multi‑instance circular buffer (FIFO) with opaque handle.
 *
 * Supports:
 *  - Configurable block size and capacity.
 *  - Custom memory allocators.
 *  - Static buffer creation (no dynamic allocation).
 *  - High/low watermark callbacks.
 *  - Occupancy statistics (max/min seen).
 *  - Optional thread‑safety (compile‑time switch).
 *
 * ## Usage (dynamic):
 * @code
 *   circular_buffer_t *cb = cb_create(64, 10, my_malloc, my_free);
 *   if (!cb) { ... }
 *   uint8_t data[64] = {...};
 *   cb_push(cb, data);
 *   cb_pop(cb, data);
 *   cb_destroy(cb);
 * @endcode
 *
 * ## Usage (static buffer, bare‑metal):
 * @code
 *   static uint8_t buf[64 * 10];
 *   circular_buffer_t *cb = cb_create_static(buf, 64, 10);
 *   cb_push(cb, data);
 *   // no need to destroy – but you can still call cb_destroy (it won't free buf)
 * @endcode
 */

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Configuration to enable multi threaded safety
 * 
 */
//------------------------------------------------------------------------------------------
#if defined(DS_USE_THREAD_SAFETY_ARM)
    #pragma message("Building Library with Thread Safety: Bare-metal ARM Cortex-M")
    
    #define DS_USE_THREAD_SAFETY
    #define DS_LOCK_T      uint32_t
    #define DS_LOCK_INIT   0
    #define DS_LOCK(primask)   do { primask = __get_PRIMASK(); __disable_irq(); } while(0)
    #define DS_UNLOCK(primask) __set_PRIMASK(primask)

#elif defined(DS_USE_THREAD_SAFETY_FreeRTOS)
    #pragma message("Building Library with Thread Safety: FreeRTOS Semaphores")

    #define DS_USE_THREAD_SAFETY
    #include "FreeRTOS.h"
    #include "semphr.h"
    #define DS_LOCK_T      SemaphoreHandle_t
    #define DS_LOCK_INIT   NULL          
    #define DS_LOCK(sem)   xSemaphoreTake(sem, portMAX_DELAY)
    #define DS_UNLOCK(sem) xSemaphoreGive(sem)

#elif defined(DS_USE_THREAD_SAFETY_POSIX)
    #pragma message("Building Library with Thread Safety: POSIX Mutexes (PC Test Target)")

    #define DS_USE_THREAD_SAFETY
    #include <pthread.h>
    #include <sched.h> 
    #define DS_LOCK_T      pthread_mutex_t
    #define DS_LOCK_INIT   PTHREAD_MUTEX_INITIALIZER
    #define DS_LOCK(mtx)   pthread_mutex_lock(mtx)
    #define DS_UNLOCK(mtx) pthread_mutex_unlock(mtx)

#else
    #pragma message("Building Library with Thread Safety: DISABLED (Single-threaded Mode)")
    // Explicitly clean up any macro definitions
    #define DS_LOCK_T      int
    #define DS_LOCK_INIT   0
    #define DS_LOCK(x)     ((void)(x))
    #define DS_UNLOCK(x)   ((void)(x)) 

    #undef DS_USE_THREAD_SAFETY
#endif
//------------------------------------------------------------------------------------------

/**
 * @brief Opaque handle for a circular buffer instance.
 *        The actual struct is defined in the .c file.
 */
typedef struct circular_buffer_t circular_buffer_t;

/** @brief Custom memory allocation function signature. */
typedef void* (*cb_malloc_fn)(size_t size);

/** @brief Custom memory deallocation function signature. */
typedef void  (*cb_free_fn)(void *ptr);

/**
 * @brief Watermark callback signature.
 * @param user_data Arbitrary user pointer registered with the callback.
 * @param current_occupancy The occupancy level that triggered the callback.
 */
typedef void (*cb_watermark_cb)(void *user_data, uint32_t current_occupancy);

/* ---------- Lifetime ---------- */

/**
 * @brief Create a circular buffer with dynamically allocated storage.
 * @param block_size  Size of one element in bytes (>0).
 * @param block_count Maximum number of elements (>0).
 * @param malloc_fn   Custom allocator (NULL → standard malloc).
 * @param free_fn     Custom deallocator (NULL → standard free).
 * @param lock        (Thread‑safe only) Pointer to a lock object.
 * @return Handle to the circular buffer, or NULL on failure.
 */
circular_buffer_t* cb_create(size_t block_size, uint32_t block_count,
                             cb_malloc_fn malloc_fn, cb_free_fn free_fn
#ifdef DS_USE_THREAD_SAFETY
                             , DS_LOCK_T *lock
#endif
);

/**
 * @brief Create a circular buffer using a pre‑allocated static buffer.
 *
 * Ideal for bare‑metal systems where dynamic allocation is unavailable or
 * undesirable. The buffer must be at least `block_size * block_count` bytes.
 *
 * @param buffer      Pointer to the user‑provided memory region.
 * @param block_size  Size of one element in bytes.
 * @param block_count Maximum number of elements.
 * @param lock        (Thread‑safe only) Pointer to a lock object.
 * @return Handle, or NULL on invalid parameters.
 */
circular_buffer_t* cb_create_static(uint8_t *buffer,
                                    size_t block_size, uint32_t block_count
#ifdef DS_USE_THREAD_SAFETY
                                    , DS_LOCK_T *lock
#endif
);

/**
 * @brief Destroy a circular buffer and free associated memory.
 *
 * If the buffer was created with cb_create_static(), the user‑provided memory
 * is *not* freed – only the handle itself is released.
 *
 * @param cb Handle to destroy (safe to pass NULL).
 */
void cb_destroy(circular_buffer_t *cb);

/* ---------- Core operations ---------- */

/**
 * @brief Push one block of data into the buffer.
 * @param cb   Handle.
 * @param data Pointer to data to copy (must be at least block_size bytes).
 * @return true on success, false if buffer is full.
 */
bool cb_push(circular_buffer_t *cb, const void *data);

/**
 * @brief Pop one block of data from the buffer.
 * @param cb       Handle.
 * @param data_out Destination buffer (must be at least block_size bytes).
 * @return true on success, false if buffer is empty.
 */
bool cb_pop(circular_buffer_t *cb, void *data_out);

/**
 * @brief Peek at the oldest element without removing it.
 * @param cb       Handle.
 * @param data_out Destination buffer.
 * @return true on success, false if buffer is empty.
 */
bool cb_peek(const circular_buffer_t *cb, void *data_out);

/* ---------- State queries ---------- */

/** @brief Number of occupied blocks. */
uint32_t cb_occupancy(const circular_buffer_t *cb);

/** @brief true if buffer contains no data. */
bool cb_is_empty(const circular_buffer_t *cb);

/** @brief true if no more elements can be pushed. */
bool cb_is_full(const circular_buffer_t *cb);

/* ---------- Watermark configuration ---------- */

/**
 * @brief Register a high‑watermark callback.
 *
 * The callback fires each time occupancy reaches or exceeds `threshold`.
 * Pass threshold=0 or cb=NULL to disable.
 */
void cb_set_high_watermark(circular_buffer_t *cb, uint32_t threshold,
                           cb_watermark_cb cb_fn, void *user_data);

/**
 * @brief Register a low‑watermark callback.
 *
 * The callback fires each time occupancy drops below `threshold`.
 * Pass threshold=0 or cb=NULL to disable.
 */
void cb_set_low_watermark(circular_buffer_t *cb, uint32_t threshold,
                          cb_watermark_cb cb_fn, void *user_data);

/* ---------- Occupancy statistics ---------- */

/** @brief Maximum occupancy observed since last reset. */
uint32_t cb_get_max_occupancy(const circular_buffer_t *cb);

/** @brief Minimum occupancy observed since last reset. */
uint32_t cb_get_min_occupancy(const circular_buffer_t *cb);

/** @brief Reset statistics to the current occupancy level. */
void cb_reset_occupancy_stats(circular_buffer_t *cb);

#endif /* CIRCULAR_BUFFER_H */