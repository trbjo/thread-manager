#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <immintrin.h>

#define MAX_SLOTS 512
#define CACHE_LINE_SIZE 64
#define WRAP_SLOT(x) ((x) & (MAX_SLOTS - 1))
#define EMPTY 0
#define DONE 1
#define VIRTUAL_ADDRESS_MASK 0xFFFFFFFFFFFFULL

#define a_load(ptr)         atomic_load_explicit((ptr), memory_order_relaxed)
#define a_inc(ptr)          atomic_fetch_add_explicit((ptr), 1ULL, memory_order_relaxed)
#define a_cmp_exchange(ptr, val, des) atomic_compare_exchange_weak_explicit((ptr), (val), (des), memory_order_relaxed, memory_order_relaxed)
#define ALWAYS_INLINE __attribute__((__always_inline__))
#define uint128_t __uint128_t

typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

typedef struct {
    _Alignas(16) uint128_t value;
    char padding[CACHE_LINE_SIZE - sizeof(uint128_t)];
} CacheAligned128_t;

struct ThreadPool {
    _Alignas(CACHE_LINE_SIZE) atomic_ulong scheduled;
    char pad1[CACHE_LINE_SIZE - sizeof(atomic_ulong)];

    _Alignas(CACHE_LINE_SIZE) atomic_ulong next;
    char pad2[CACHE_LINE_SIZE - sizeof(atomic_ulong)];

    _Alignas(CACHE_LINE_SIZE) CacheAligned128_t slots[MAX_SLOTS];

    pthread_t* threads;
    int max_threads;
} __attribute__((aligned(CACHE_LINE_SIZE)));

typedef struct ThreadPool ThreadPool;

void thread_pool_initialize(void);
void thread_pool_destroy(void);
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

ALWAYS_INLINE static inline int atomic128_compare_exchange(
    uint128_t *ptr,
    uint128_t *expected,
    uint128_t desired
) {
    int success;
    __asm__ __volatile__ (
        "lock; cmpxchg16b %0"
        : "+m" (*ptr),
          "+d" (*(uint64_t*)((char*)expected + 8)),
          "+a" (*(uint64_t*)expected),
          "=@ccz" (success)
        : "c" ((uint64_t)(desired >> 64)),
          "b" ((uint64_t)desired)
        : "cc"
    );
    return success;
}

ALWAYS_INLINE static inline uint128_t atomic128_load(uint128_t const *ptr) {
    uint128_t result;
    __asm__ __volatile__ (
        "movdqa %1, %0"
        : "=x" (result)
        : "m" (*ptr)
        : "memory"
    );
    return result;
}

#endif
