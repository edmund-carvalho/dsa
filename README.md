# Embedded Data Structures Library (C)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)]()
[![C Standard](https://img.shields.io/badge/C-C99-blue)]()

A lightweight, production‑ready collection of fundamental data structures for 32‑bit embedded systems (ARM Cortex‑M, RISC‑V, PIC32, etc.). All modules use **opaque handles**, support **multiple independent instances**, and are configurable Sfor deterministic execution under real-time bare‑metal or RTOS constraints.

---

## Features

| Data Structure | Type | Static Alloc | Watermarks | Thread‑Safe* |
| :--- | :--- | :---: | :---: | :---: |
| **Circular Buffer** | FIFO Array | ✅ | ✅ | ✅ |
| **Stack** | LIFO Array | ✅ | ✅ | ✅ |
| **Queue** | Linked FIFO | via allocator | ✅ | ✅ |
| **Doubly Linked List** | Deque | via allocator | ✅ | ✅ |

*\* Thread‑safety is **opt‑in** at compile time (zero overhead when disabled).*

### Architecture & Design Principles

* **Opaque Handles:** Complete data encapsulation; internal layout metrics are hidden inside individual implementation sources, preventing unauthorized memory access or structure clobbering.
* **Block‑Based Storage:** Elements are treated as uniform fixed‑size blocks. Deep value copies are completely managed internally by the module via vector mechanics (`memcpy`).
* **Custom Allocator Injection:** Plug in runtime allocation endpoints (`malloc`/`free`) easily—ideal for FreeRTOS heap strategies, fixed block pool managers, or zero-fragmentation static blocks.
* **Zero-Heap Static Buffers:** Spin up circular buffers and stacks directly from pre-allocated user memory spaces to bypass execution paths with dynamic allocation completely.
* **Watermark & Flow Control Monitoring:** Hook lightweight edge-triggered callbacks directly into data push/pop pipes. Perfect for detecting upstream throttle indicators, backpressure generation, and buffer overflow prevention.
* **Runtime Occupancy Statistics:** Real-time tracking of high and low historic metrics since initialization for optimal safety footprint mapping.
* **C99 Compliant:** Compiles clean out of the box using strict warnings flags (`-Wall -Wextra -Werror`) without relying on non-standard toolchain extensions.

---

## Getting Started

### Clone and Build

```bash
git clone [https://github.com/yourname/embedded-ds.git](https://github.com/yourname/embedded-ds.git)
cd embedded-ds
make  # Produces target static archive: libembedded-ds.a

```

### Link Into Your Project

Add the library to your main compilation environment setup:

```makefile
CFLAGS  += -Ipath/to/embedded-ds/include
LDFLAGS += -Lpath/to/embedded-ds -lembedded-ds

```

---

## Usage Examples

### 1. Circular Buffer (FIFO)

```c
#include "circular_buffer.h"

// Static array creation path (bare‑metal, no heap usage)
static uint8_t rx_buf[32 * 10]; // Capacity for 10 blocks of 32 bytes each
circular_buffer_t *cb = cb_create_static(rx_buf, 32, 10);

uint8_t packet[32] = {0};
// ... populate data payload ...
cb_push(cb, packet); // Producer transaction

uint8_t out[32];
if (cb_pop(cb, out)) {
    // Consumer transaction successful
}

cb_destroy(cb); // Only frees the management handle structure; keeps rx_buf intact

```

### 2. Stack (LIFO)

```c
#include "stack.h"

// Instantiated via default standard C runtime heap management handlers
stack_t *st = stack_create(sizeof(int), 20, NULL, NULL); 

int val = 42;
stack_push(st, &val);

int out_val = 0;
stack_pop(st, &out_val);
// out_val == 42

stack_destroy(st);

```

### 3. Queue (Linked List FIFO)

```c
#include "queue.h"

// Managed length threshold tracking using a dedicated custom static memory pool allocation mechanism
queue_t *q = queue_create(sizeof(sensor_data_t), 1000, pool_malloc, pool_free);

sensor_data_t sd = { .temperature = 23.5f };
queue_enqueue(q, &sd);

sensor_data_t active_node;
queue_dequeue(q, &active_node);

queue_destroy(q);

```

### 4. Doubly Linked List

```c
#include "linked_list.h"

// Limitless node constraints configured (unlimited structure capacity tracking mode)
linked_list_t *list = ll_create(sizeof(command_t), 0, NULL, NULL);

command_t cmd = { .id = 0x01 };
ll_push_back(list, &cmd);  // Insert item to structure tail
ll_push_front(list, &cmd); // Insert item to structure head
ll_pop_front(list, &cmd);  // Extract from structure head
ll_pop_back(list, &cmd);   // Extract from structure tail

ll_destroy(list);

```

---

## Deep Dive Features

### Custom Dynamic Memory Tuning

Passing `NULL` to the creation methods automatically forces fallback hooks to the standard standard C library allocators. For bare-metal applications or deterministic RTOS design targets, you can pass custom routines directly into the signature structure:

```c
static uint8_t arena[4096];
static size_t  arena_idx = 0;

void *my_malloc(size_t size) {
    if (arena_idx + size > sizeof(arena)) return NULL;
    void *p = &arena[arena_idx];
    arena_idx += size;
    return p;
}

void my_free(void *p) { 
    (void)p; // Static arena pattern tracking bypass rule
}

// Instantiate structure completely backed by custom memory workspace setup
circular_buffer_t *cb = cb_create(16, 64, my_malloc, my_free);

```

### Flow Control with Watermarks

Automate system flow control loops by establishing non-polling threshold notifications:

```c
void high_water_callback(void *user_data, uint32_t occupancy) {
    // Assert hardware throttle pins, send standard pause network frames, or signal RTOS events
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
}

// Configures callback to signal immediately when buffer space processing reaches 50 units occupancy
cb_set_high_watermark(cb, 50, high_water_callback, NULL);

```

### Concurrency & Thread‑Safety Architecture

To run safely inside asynchronous multi-tasking topologies:

1. Enable the appropriate architectural macro configuration flag target inside the library compile pipeline (`-DDS_USE_THREAD_SAFETY_POSIX`, `-DDS_USE_THREAD_SAFETY_ARM`, or `-DDS_USE_THREAD_SAFETY_FreeRTOS`).
2. Supply your synchronization lock initialization object target address straight during compilation instantiation stages:

```c
#ifdef DS_USE_THREAD_SAFETY
pthread_mutex_t queue_lock;
pthread_mutex_init(&queue_lock, NULL);

// Instantiate completely thread-safe atomic execution container context
queue_t *safe_queue = queue_create(sizeof(int), 128, NULL, NULL, &queue_lock);
#endif

```

> **Note:** When thread-safety macros are completely disabled or left undefined, all associated locking mechanisms resolve entirely to zero-overhead empty lines at compile time with absolute zero target runtime penalties.

```