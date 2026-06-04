// SPDX-License-Identifier: MIT
/**
 * @file linkedList.h
 * @brief Multi‑instance doubly linked list with opaque handle.
 */

#ifndef _LINKED_LIST_H
#define _LINKED_LIST_H

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

typedef struct linked_list_t linked_list_t;

typedef void* (*ll_malloc_fn)(size_t size);
typedef void  (*ll_free_fn)(void *ptr);
typedef void  (*ll_watermark_cb)(void *user_data, uint32_t occupancy);

// Now the compiler safely recognizes DS_LOCK_T here
linked_list_t* ll_create(size_t block_size, uint32_t max_elements,
                         ll_malloc_fn m, ll_free_fn f
                         , DS_LOCK_T *lock
);
void ll_destroy(linked_list_t *l);

bool     ll_push_back(linked_list_t *l, const void *data);
bool     ll_pop_front(linked_list_t *l, void *data_out);
bool     ll_push_front(linked_list_t *l, const void *data);
bool     ll_pop_back(linked_list_t *l, void *data_out);
bool     ll_peek_front(const linked_list_t *l, void *data_out);
bool     ll_peek_back(const linked_list_t *l, void *data_out);
uint32_t ll_occupancy(const linked_list_t *l);
bool     ll_is_empty(const linked_list_t *l);
bool     ll_is_full(const linked_list_t *l);

void ll_set_high_watermark(linked_list_t *l, uint32_t threshold,
                           ll_watermark_cb cb, void *user_data);
void ll_set_low_watermark(linked_list_t *l, uint32_t threshold,
                          ll_watermark_cb cb, void *user_data);

uint32_t ll_get_max_occupancy(const linked_list_t *l);
uint32_t ll_get_min_occupancy(const linked_list_t *l);
void     ll_reset_occupancy_stats(linked_list_t *l);

#endif /* _LINKED_LIST_H */