// SPDX-License-Identifier: MIT
/**
 * @file stack.h
 * @brief Multi‑instance LIFO stack with opaque handle.
 *
 * Supports dynamic or static buffer creation, custom allocators,
 * watermarks, and optional thread‑safety.
 */

#ifndef STACK_H
#define STACK_H

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
/** @brief Opaque stack handle. */
typedef struct stack_t stack_t;

typedef void* (*stack_malloc_fn)(size_t size);
typedef void  (*stack_free_fn)(void *ptr);
typedef void  (*stack_watermark_cb)(void *user_data, uint32_t occupancy);

/* Lifetime */
stack_t* stack_create(size_t block_size, uint32_t capacity,
                      stack_malloc_fn m, stack_free_fn f
#ifdef DS_USE_THREAD_SAFETY
                      , DS_LOCK_T *lock
#endif
);
stack_t* stack_create_static(uint8_t *buffer, size_t block_size, uint32_t capacity
#ifdef DS_USE_THREAD_SAFETY
                             , DS_LOCK_T *lock
#endif
);
void stack_destroy(stack_t *s);

/* Operations */
bool     stack_push(stack_t *s, const void *data);
bool     stack_pop(stack_t *s, void *data_out);
bool     stack_peek(const stack_t *s, void *data_out);
uint32_t stack_occupancy(const stack_t *s);
bool     stack_is_empty(const stack_t *s);
bool     stack_is_full(const stack_t *s);

/* Watermarks */
void stack_set_high_watermark(stack_t *s, uint32_t threshold,
                              stack_watermark_cb cb, void *user_data);
void stack_set_low_watermark(stack_t *s, uint32_t threshold,
                             stack_watermark_cb cb, void *user_data);

/* Statistics */
uint32_t stack_get_max_occupancy(const stack_t *s);
uint32_t stack_get_min_occupancy(const stack_t *s);
void     stack_reset_occupancy_stats(stack_t *s);

#endif /* STACK_H */