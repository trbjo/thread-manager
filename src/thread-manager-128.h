#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define uint128_t __uint128_t

#define EMPTY 0
#define SLEEPING 1

#define PTR_ALIGN_SHIFT 4
#define PTR_BITS 43
#define SCND_PTR_BITS 45
#define SECOND_PTR 44
#define SCND_PTR_ALIGN_SHIFT 2
#define DIFF_START 89
#define DIFF_BITS 38
#define PTR_MASK ((1ULL << PTR_BITS) - 1)
#define SCND_PTR_MASK ((1ULL << SCND_PTR_BITS) - 1)
#define SIGN_EXTEND_SHIFT (64 - DIFF_BITS)
#define IS_THREAD_ID(x) ((x >> 64) != 0 && (x >> 112) == 0 && (x << 64) == 0)
#define IS_TASK(x) ((((uint64_t)x) > EMPTY) || (x == EXIT))

#define EXIT ((uint128_t)1 << 127)
#define DATA_FLAG ((uint128_t)1 << (PTR_BITS))

#define second_ptr(t) (int64_t)(((t >> SECOND_PTR) & SCND_PTR_MASK) << SCND_PTR_ALIGN_SHIFT)
#define third_ptr(t) \
    ({ \
        int64_t offset = ((int64_t)((t >> DIFF_START) << SIGN_EXTEND_SHIFT)) >> SIGN_EXTEND_SHIFT; \
        offset ? (int64_t)(((int64_t)(t & PTR_MASK) + offset) << PTR_ALIGN_SHIFT) : 0; \
    })

#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

#define atomic_inc(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_RELAXED)
#define atomic_dec(ptr) __atomic_fetch_sub((ptr), 1, __ATOMIC_RELAXED)

static inline int __attribute__((always_inline)) __atomic128_cas(
    __uint128_t *address,
    __uint128_t *expected,
    __uint128_t desired
) {
    char success;
    __asm__ __volatile__ (
        "lock; cmpxchg16b %0"
        : "+m" (*address),
          "+A" (*expected),
          "=@ccz" (success)
        : "c" ((uint64_t)(desired >> 64)),
          "b" ((uint64_t)desired)
        : "memory"
    );
    return success;
}

#define atomic128_cas(ptr, expected, desired) \
    __atomic128_cas(((__uint128_t*)ptr), expected, desired)

static inline __uint128_t __atomic128_load(__uint128_t const *address) {
    __uint128_t result;
    __asm__ __volatile__ (
        "movdqa %1, %0"
        : "=x" (result)
        : "m" (*address)
        : "memory"
    );
    return result;
}

#define atomic128_load(ptr) \
    __atomic128_load(((__uint128_t*)ptr))

static inline void __atomic128_store(__uint128_t *address, __uint128_t value) {
    __asm__ __volatile__ (
        "movdqa %1, %0"
        : "=m" (*address)
        : "x" (value)
        : "memory"
    );
}

#define atomic128_store(ptr, val) \
    __atomic128_store(((__uint128_t*)ptr), val)

typedef void (*TaskFunc)(void* data);
typedef void (*TaskDestroy)(void* data);

#define thread_pool_join(thread_id) pthread_join(thread_id, NULL)

void thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy);
#define thread_pool_run(func, data, destroy) thread_pool_schedule_task(func, data, destroy)

void thread_pool_join_all();

void thread_pool_init(uint16_t num_workers, int pin_cores);
int thread_pool_num_cores(void);

#endif
