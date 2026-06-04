#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include "queue.h"

#define MAX_COUNT        100000  /* Total items to pass through the queue */
#define QUEUE_MAX_NODES  50000      /* Artificially small capacity to force contention */

/* Thread context tracking data */
typedef struct {
    queue_t  *list;
    uint64_t  expected_sum;  /* Maintained by producer */
    uint64_t  consumer_sum;  /* Maintained by consumer */
} thread_ctx_t;

/* --- Producer Thread --- */
void* producer_f(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    ctx->expected_sum = 0;
    
    for (int i = 1; i <= MAX_COUNT; i++) {
        // Accumulate checksum before pushing
        ctx->expected_sum += i;

        // Attempt to push to the back of the queue
        while (!queue_enqueue(ctx->list, &i)) {
            sched_yield(); // Yield CPU slice if the list is full
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
        if (queue_dequeue(ctx->list, &val)) {
            // Accumulate checksum from popped data instead of printing
            ctx->consumer_sum += val;
            items_received++;
        } else {
            // List is empty, yield CPU to let the producer work
            sched_yield(); 
        }
    }
    return NULL;
}

/* --- High/Low Watermark Callbacks --- */
void high_watermark_reached(void *user_data, uint32_t occupancy) {
    // (void)user_data;
    // // Keep IO lightweight inside high-frequency execution
    // printf("[CB] High watermark triggered at occupancy: %u\n", occupancy);
}

void low_watermark_reached(void *user_data, uint32_t occupancy) {
    // (void)user_data;
    // printf("[CB] Low watermark triggered at occupancy: %u\n", occupancy);
}

int main(void) {
    printf("==================================================\n");
    printf(" Starting Thread-Safe Queue Stress Test Target     \n");
    printf("==================================================\n");

    /* 1. Initialize POSIX Mutex for Thread Safety Configuration */
    pthread_mutex_t queue_mutex;
    if (pthread_mutex_init(&queue_mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        return EXIT_FAILURE;
    }

    /* 2. Create the Thread-Safe Queue Instance */
    queue_t *my_queue = queue_create(sizeof(int), QUEUE_MAX_NODES, NULL, NULL, &queue_mutex);
    if (!my_queue) {
        fprintf(stderr, "Failed to create queue.\n");
        pthread_mutex_destroy(&queue_mutex);
        return EXIT_FAILURE;
    }

    /* 3. Setup Context and Optional Watermarks */
    thread_ctx_t context = {
        .list = my_queue,
        .expected_sum = 0,
        .consumer_sum = 0
    };

    queue_set_high_watermark(my_queue, (QUEUE_MAX_NODES * 8) / 10, high_watermark_reached, NULL); // 95%
    queue_set_low_watermark(my_queue,  (QUEUE_MAX_NODES * 2) / 10, low_watermark_reached, NULL);  // 5%

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
    printf("  - Max Queue Occupancy hit:  %u / %u\n", 
           queue_get_max_occupancy(my_queue), QUEUE_MAX_NODES);
    printf("  - Ending Queue Occupancy:   %u\n", queue_occupancy(my_queue));

    bool success = true;
    if (context.expected_sum != context.consumer_sum) {
        printf("TEST FAILED: Checksum Mismatch (Data Corruption/Loss detected).\n");
        success = false;
    } else if (queue_occupancy(my_queue) != 0) {
        printf("TEST FAILED: Memory leaked. Remaining elements left hanging.\n");
        success = false;
    } else {
        printf("TEST PASSED: Thread-safety verified successfully.\n");
    }

    /* 7. Clean up Resources */
    queue_destroy(my_queue);
    pthread_mutex_destroy(&queue_mutex);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}