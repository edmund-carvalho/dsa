#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include "circularBuffer.h"

#define MAX_COUNT        10  /* Total items to cycle through the buffer */
#define BUFFER_CAPACITY  50      /* Small capacity to heavily force thread contention */

/* Thread context tracking data */
typedef struct {
    circular_buffer_t *cb;
    uint64_t           expected_sum;  /* Maintained by producer */
    uint64_t           consumer_sum;  /* Maintained by consumer */
} thread_ctx_t;

/* --- Producer Thread --- */
void* producer_f(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    ctx->expected_sum = 0;
    
    for (int i = 1; i <= MAX_COUNT; i++) {
        // Accumulate checksum before pushing
        ctx->expected_sum += i;

        // Attempt to push to the circular buffer
        while (!cb_push(ctx->cb, &i)) {
            sched_yield(); // Yield CPU slice if the buffer is full
        }
    }
    return NULL;
}

/* --- Consumer Thread --- */
void* consumer_f(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    int items_received = 0;
    ctx->consumer_sum = 0;
    
    while (items_received < MAX_COUNT) {
        int val = 0;
        if (cb_pop(ctx->cb, &val)) {
            // Accumulate checksum from popped data
            ctx->consumer_sum += val;
            items_received++;
        } else {
            // Buffer is empty, yield CPU to let the producer work
            sched_yield(); 
        }
    }
    return NULL;
}

int main(void) {
    printf("==================================================\n");
    printf(" Starting Thread-Safe Circular Buffer Stress Test \n");
    printf("==================================================\n");

    /* 1. Initialize POSIX Mutex for Thread Safety Configuration */
    pthread_mutex_t cb_mutex;
    if (pthread_mutex_init(&cb_mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        return EXIT_FAILURE;
    }

    /* 2. Create the Thread-Safe Circular Buffer Instance */
    circular_buffer_t *my_cb = cb_create(sizeof(int), BUFFER_CAPACITY, NULL, NULL, &cb_mutex);
    if (!my_cb) {
        fprintf(stderr, "Failed to create circular buffer.\n");
        pthread_mutex_destroy(&cb_mutex);
        return EXIT_FAILURE;
    }

    /* 3. Setup Context tracking */
    thread_ctx_t context = {
        .cb = my_cb,
        .expected_sum = 0,
        .consumer_sum = 0
    };

    /* 4. Spawn Producer and Consumer Worker Threads */
    pthread_t producer_thread;
    pthread_t consumer_thread;

    if (pthread_create(&producer_thread, NULL, producer_f, &context) != 0) {
        perror("Failed to create producer thread");
        return EXIT_FAILURE;
    }

    if (pthread_create(&consumer_thread, NULL, consumer_f, &context) != 0) {
        perror("Failed to create consumer thread");
        return EXIT_FAILURE;
    }

    /* 5. Await Thread Processing Completion */
    pthread_join(producer_thread, NULL);
    pthread_join(consumer_thread, NULL);

    /* 6. Verify and Report Results */
    printf("\n[Test Summary]:\n");
    printf("  - Total elements processed: %d\n", MAX_COUNT);
    printf("  - Producer Checksum:       %llu\n", context.expected_sum);
    printf("  - Consumer Checksum:       %llu\n", context.consumer_sum);
    printf("  - Max Buffer Occupancy hit: %u / %u\n", 
           cb_get_max_occupancy(my_cb), BUFFER_CAPACITY);
    printf("  - Ending Buffer Occupancy:  %u\n", cb_occupancy(my_cb));

    bool success = true;
    if (context.expected_sum != context.consumer_sum) {
        printf("\n❌ TEST FAILED: Checksum Mismatch (Data Corruption/Loss detected).\n");
        success = false;
    } else if (cb_occupancy(my_cb) != 0) {
        printf("\n❌ TEST FAILED: Elements leaked or left stuck in circular buffer.\n");
        success = false;
    } else {
        printf("\n✅ TEST PASSED: Circular Buffer thread-safety verified successfully.\n");
    }

    /* 7. Clean up Resources */
    cb_destroy(my_cb);
    pthread_mutex_destroy(&cb_mutex);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}