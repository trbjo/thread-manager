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
#define DONE 1ULL
#define VIRTUAL_ADDRESS_MASK 0xFFFFFFFFFFFFULL

#define a_load(ptr)         atomic_load_explicit((ptr), memory_order_relaxed)
#define a_inc(ptr)          atomic_fetch_add_explicit((ptr), 1ULL, memory_order_relaxed)
#define a_cmp_exchange(ptr, val, des) atomic_compare_exchange_weak_explicit((ptr), (val), (des), memory_order_relaxed, memory_order_relaxed)

typedef struct {
    _Alignas(CACHE_LINE_SIZE)
    uint64_t low;
    uint64_t high;
    char padding[CACHE_LINE_SIZE - 2 * sizeof(uint64_t)];
} atomic128_t;

typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

struct ThreadPool {
    _Alignas(CACHE_LINE_SIZE) atomic_ulong scheduled;
    char pad1[CACHE_LINE_SIZE - sizeof(atomic_ulong)];

    _Alignas(CACHE_LINE_SIZE) atomic_ulong next;
    char pad2[CACHE_LINE_SIZE - sizeof(atomic_ulong)];

    _Alignas(CACHE_LINE_SIZE) atomic128_t slots[MAX_SLOTS];

    pthread_t* threads;
    int max_threads;
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct ThreadPool ThreadPool;

void thread_pool_initialize(void);
void thread_pool_destroy(void);
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

static inline int atomic128_compare_exchange(
    atomic128_t *ptr,
    uint64_t *expected_high, uint64_t *expected_low,
    uint64_t desired_high, uint64_t desired_low
) {
    int success;
    __asm__ __volatile__ (
        "lock; cmpxchg16b %0"
        : "+m" (*ptr),
          "+d" (*expected_high),
          "+a" (*expected_low),
          "=@ccz" (success)
        : "c" (desired_high),
          "b" (desired_low)
        : "cc"
    );
    return success;
}

static inline void atomic128_exchange(atomic128_t *ptr, uint64_t *old_high, uint64_t *old_low,
                                    uint64_t new_high, uint64_t new_low) {
    __asm__ __volatile__ (
        "lock; cmpxchg16b %0"
        : "+m" (*ptr),
          "=a" (*old_low), "=d" (*old_high)
        : "b" (new_low),
          "c" (new_high),
          "a" (ptr->low), "d" (ptr->high)
        : "cc"
    );
}

#endif
