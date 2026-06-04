// SPDX-License-Identifier: MIT
/**
 * @file queue.h
 * @brief Multi‑instance FIFO queue with opaque handle.
 *
 * Implemented as a singly‑linked list. Supports maximum node limit,
 * custom allocators (e.g., static node pool), watermarks, and thread‑safety.
 */

#ifndef QUEUE_H
#define QUEUE_H

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

/* ------------------------------------------------------------------ */
/* Public API Types and Prototypes                                    */
/* ------------------------------------------------------------------ */
typedef struct queue_t queue_t;

typedef void* (*queue_malloc_fn)(size_t size);
typedef void  (*queue_free_fn)(void *ptr);
typedef void  (*queue_watermark_cb)(void *user_data, uint32_t occupancy);

queue_t* queue_create(size_t block_size, uint32_t max_nodes,
                      queue_malloc_fn m, queue_free_fn f
#ifdef DS_USE_THREAD_SAFETY
                      , DS_LOCK_T *lock
#endif
);
void queue_destroy(queue_t *q);

bool     queue_enqueue(queue_t *q, const void *data);
bool     queue_dequeue(queue_t *q, void *data_out);
bool     queue_peek(const queue_t *q, void *data_out);
uint32_t queue_occupancy(const queue_t *q);
bool     queue_is_empty(const queue_t *q);
bool     queue_is_full(const queue_t *q);

void queue_set_high_watermark(queue_t *q, uint32_t threshold,
                              queue_watermark_cb cb, void *user_data);
void queue_set_low_watermark(queue_t *q, uint32_t threshold,
                             queue_watermark_cb cb, void *user_data);

uint32_t queue_get_max_occupancy(const queue_t *q);
uint32_t queue_get_min_occupancy(const queue_t *q);
void     queue_reset_occupancy_stats(queue_t *q);

#endif /* QUEUE_H */