#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Pull in standard POSIX threading and scheduling headers
#include <pthread.h>
#include <sched.h> 

#include "linkedList.h"

#define MAX_COUNT 10000

// Shared structure passed to both threads
typedef struct {
    linked_list_t *list;
    uint64_t expected_sum;
    uint64_t consumer_sum;
} thread_ctx_t;

// --- Producer Thread ---
void* producer_f(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    ctx->expected_sum = 0;
    
    for (int i = 1; i <= MAX_COUNT; i++) {
        // Accumulate checksum before pushing
        ctx->expected_sum += i;

        // Attempt to push to the back of the queue
        while (!ll_push_back(ctx->list, &i)) {
            sched_yield(); // Yield CPU slice if the list is full
        }
    }
    return NULL;
}

// --- Consumer Thread ---
void* consumer_f(void *arg) {
    thread_ctx_t *ctx = (thread_ctx_t *)arg;
    int items_received = 0;
    ctx->consumer_sum = 0;
    
    while (items_received < MAX_COUNT) {
        int val = 0;
        if (ll_pop_front(ctx->list, &val)) {
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

int main(void) {
    // Instantiate a POSIX mutex using the static initializer macro
    pthread_mutex_t list_mutex = DS_LOCK_INIT;

    // Create the opaque list instance, passing the address of our mutex
    linked_list_t *my_list = ll_create(sizeof(int), 0, NULL, NULL, &list_mutex);
    if (!my_list) {
        printf("Error: Failed to create linked list.\n");
        return 1;
    }

    // Initialize tracking context
    thread_ctx_t context = {
        .list = my_list,
        .expected_sum = 0,
        .consumer_sum = 0
    };

    pthread_t producer_thread;
    pthread_t consumer_thread;

    printf("Starting multi-threaded checksum test (%d elements)...\n", MAX_COUNT);

    // Create the POSIX threads
    if (pthread_create(&producer_thread, NULL, producer_f, &context) != 0 ||
        pthread_create(&consumer_thread, NULL, consumer_f, &context) != 0) {
        printf("Error: Thread creation failed.\n");
        ll_destroy(my_list);
        pthread_mutex_destroy(&list_mutex);
        return 1;
    }

    // Wait for both threads to finish their execution loops
    pthread_join(producer_thread, NULL);
    pthread_join(consumer_thread, NULL);

    // Verify Checksums
    printf("\n--- Test Results ---\n");
    printf("Producer Expected Sum : %llu\n", (unsigned long long)context.expected_sum);
    printf("Consumer Received Sum : %llu\n", (unsigned long long)context.consumer_sum);

    if (context.expected_sum == context.consumer_sum) {
        printf("SUCCESS: Checksums match perfectly! Thread safety verification passed.\n");
    } else {
        printf("ERROR: Checksum mismatch! Data corruption detected.\n");
    }

    printf("Max occupancy reached: %u\n", ll_get_max_occupancy(my_list));

    // Cleanup resources
    ll_destroy(my_list);
    pthread_mutex_destroy(&list_mutex);

    return 0;
}