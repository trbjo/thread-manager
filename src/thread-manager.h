#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define MAX_SLOTS 512
#define CACHE_LINE_SIZE 64
#define WRAP_SLOT(x) ((x) & (MAX_SLOTS - 1))
#define EMPTY 0ULL
#define IDLE 1ULL
#define SHUTDOWN (1ULL << 63)
#define VIRTUAL_ADDRESS_MASK 0xFFFFFFFFFFFFULL

#define a_load(ptr)         atomic_load_explicit((ptr), memory_order_relaxed)
#define a_inc(ptr)          atomic_fetch_add_explicit((ptr), 1ULL, memory_order_relaxed)
#define a_exchange(ptr, val) atomic_exchange_explicit((ptr), (val), memory_order_acq_rel)
#define a_cmp_exchange(ptr, val, des) atomic_compare_exchange_weak_explicit((ptr), (val), (des), memory_order_release, memory_order_relaxed)

typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

struct TaskSlot {
    uint64_t funcs;
    uint64_t data;
};
typedef struct TaskSlot TaskSlot;

struct padded_atomic_ulong {
    _Alignas(CACHE_LINE_SIZE) atomic_ulong value;
    char padding[CACHE_LINE_SIZE - sizeof(atomic_ulong)];
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct padded_atomic_ulong padded_atomic_ulong;

struct ThreadPool {
    padded_atomic_ulong scheduled;
    padded_atomic_ulong next;

    _Alignas(CACHE_LINE_SIZE) padded_atomic_ulong slots[MAX_SLOTS];

    pthread_t* threads;
    int max_threads;
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct ThreadPool ThreadPool;

void thread_pool_initialize();
void thread_pool_destroy();
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

#endif
