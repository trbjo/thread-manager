#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <pthread.h>

#define MAX_SLOTS 256 // must be power of two
#define CACHE_LINE_SIZE 64
#define WRAP_SLOT(x) ((x) & (MAX_SLOTS - 1))
#define VIRTUAL_ADDRESS_MASK 0xFFFFFFFFFFFFULL  // 48-bit mask for virtual address space

struct padded_atomic_uint {
    _Alignas(CACHE_LINE_SIZE) atomic_uint value;
    char padding[CACHE_LINE_SIZE - sizeof(atomic_uint)];
} __attribute__((aligned(CACHE_LINE_SIZE)));

struct padded_atomic_ulong {
    _Alignas(CACHE_LINE_SIZE) atomic_ulong value;
    char padding[CACHE_LINE_SIZE - sizeof(atomic_ulong)];
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct padded_atomic_uint padded_atomic_uint;
typedef struct padded_atomic_ulong padded_atomic_ulong;

struct ThreadPool {
    padded_atomic_uint push_counter;
    padded_atomic_uint pull_counter;
    padded_atomic_uint shutdown;

    _Alignas(CACHE_LINE_SIZE) padded_atomic_ulong task_handles[MAX_SLOTS];
    _Alignas(CACHE_LINE_SIZE) padded_atomic_ulong task_data[MAX_SLOTS];

    pthread_t* threads;
    int max_threads;
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct ThreadPool ThreadPool;

// Public API functions
typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

void thread_pool_initialize();
void thread_pool_destroy();
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

#endif
