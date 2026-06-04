#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include "stack.h"

#define MAX_COUNT        100000  /* Total items to cycle through the stack */
#define STACK_CAPACITY   5000    /* Small capacity to heavily force contention */

/* Thread context tracking data */
typedef struct {
    stack_t  *stack;
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

        // Attempt to push to the top of the stack
        while (!stack_push(ctx->stack, &i)) {
            sched_yield(); // Yield CPU slice if the stack is full
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
        if (stack_pop(ctx->stack, &val)) {
            // Accumulate checksum from popped data
            ctx->consumer_sum += val;
            items_received++;
        } else {
            // Stack is empty, yield CPU to let the producer work
            sched_yield(); 
        }
    }
    return NULL;
}

int main(void) {
    printf("==================================================\n");
    printf(" Starting Thread-Safe Stack Stress Test Target     \n");
    printf("==================================================\n");

    /* 1. Initialize POSIX Mutex for Thread Safety Configuration */
    pthread_mutex_t stack_mutex;
    if (pthread_mutex_init(&stack_mutex, NULL) != 0) {
        perror("Failed to initialize mutex");
        return EXIT_FAILURE;
    }

    /* 2. Create the Thread-Safe Stack Instance */
    stack_t *my_stack = stack_create(sizeof(int), STACK_CAPACITY, NULL, NULL, &stack_mutex);
    if (!my_stack) {
        fprintf(stderr, "Failed to create stack.\n");
        pthread_mutex_destroy(&stack_mutex);
        return EXIT_FAILURE;
    }

    /* 3. Setup Context tracking */
    thread_ctx_t context = {
        .stack = my_stack,
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
    printf("  - Max Stack Occupancy hit:  %u / %u\n", stack_get_max_occupancy(my_stack), STACK_CAPACITY);
    printf("  - Ending Stack Occupancy:   %u\n", stack_occupancy(my_stack));

    bool success = true;
    if (context.expected_sum != context.consumer_sum) {
        printf("TEST FAILED: Checksum Mismatch (Data Corruption/Loss detected).\n");
        success = false;
    } else if (stack_occupancy(my_stack) != 0) {
        printf("TEST FAILED: Elements leaked or left stuck on stack.\n");
        success = false;
    } else {
        printf("TEST PASSED: Stack thread-safety verified successfully.\n");
    }

    /* 7. Clean up Resources */
    stack_destroy(my_stack);
    pthread_mutex_destroy(&stack_mutex);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}