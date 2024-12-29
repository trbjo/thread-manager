#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <pthread.h>

// Core configuration
#define BITS_PER_CHUNK 64
#define CHUNKS 16
#define MAX_SLOTS (CHUNKS * BITS_PER_CHUNK)
#define CACHE_LINE_SIZE 64
#define CHUNK_FULL (~0UL)

#define BIT_MASK(n)              (1UL << (n))
#define SET_BIT_MASK(n)          BIT_MASK(n)
#define UNSET_BIT_MASK(n)        (~BIT_MASK(n))
#define LOWEST_SET_BIT(value)    __builtin_ctzll(value)
#define LOWEST_UNSET_BIT(value)  __builtin_ctzll(~(value))
#define CLEAR_LOWEST_BIT(value)  ((value) & UNSET_BIT_MASK(LOWEST_SET_BIT(value)))
#define SET_BIT(value, n)        ((value) | SET_BIT_MASK(n))

// Calculate number of bits needed to represent MAX_SLOTS
#define COMPUTE_SLOT_BITS(n) \
    ((n) < (1 << 8) ? 8 : \
     (n) < (1 << 9) ? 9 : \
     (n) < (1 << 10) ? 10 : \
     (n) < (1 << 11) ? 11 : \
     (n) < (1 << 12) ? 12 : \
     (n) < (1 << 13) ? 13 : \
     (n) < (1 << 14) ? 14 : 15)

#define SLOT_BITS COMPUTE_SLOT_BITS(MAX_SLOTS)

// Verify our configuration at compile time
#if (MAX_SLOTS != (CHUNKS * BITS_PER_CHUNK))
#error "MAX_SLOTS must equal CHUNKS * BITS_PER_CHUNK"
#endif

#if (1 << SLOT_BITS) < MAX_SLOTS
#error "SLOT_BITS insufficient to represent MAX_SLOTS"
#endif

// Function pointer types
typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

struct padded_atomic_ulong {
    _Alignas(CACHE_LINE_SIZE) atomic_ulong value;
    char padding[CACHE_LINE_SIZE - sizeof(atomic_ulong)];
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct padded_atomic_ulong padded_atomic_ulong;

struct TaskHandle {
    TaskFunc task;                 // 8 bytes (function pointer)
    void* task_user_data;          // 8 bytes
    TaskDestroy task_destroy;      // 8 bytes (function pointer)
    char padding[CACHE_LINE_SIZE - (3 * sizeof(void*))];
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct TaskHandle TaskHandle;

struct ThreadPool {
    atomic_int shutdown;
    char pad1[CACHE_LINE_SIZE - sizeof(atomic_int)];

    atomic_int tasks;
    char pad2[CACHE_LINE_SIZE - sizeof(atomic_int)];

    atomic_uint current_chunk;
    char pad3[CACHE_LINE_SIZE - sizeof(atomic_uint)];

    struct padded_atomic_ulong ready[CHUNKS];
    struct padded_atomic_ulong claimed[CHUNKS];

    TaskHandle* task_handles;

    pthread_t* threads;
    int max_threads;
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct ThreadPool ThreadPool;

// Public API functions
void thread_pool_initialize();
void thread_pool_destroy();
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

#endif // THREAD_MANAGER_H
