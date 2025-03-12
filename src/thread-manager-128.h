#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define MAX_SLOTS (((uint8_t)~0) + 1)
#define uint128_t __uint128_t

#define EMPTY 0
#define SLEEPING 1
#define WAKING_UP 2

#define PTR_ALIGN_SHIFT 4
#define PTR_BITS 43
#define SCND_PTR_BITS 45
#define SECOND_PTR 45
#define SCND_PTR_ALIGN_SHIFT 2
#define DIFF_START 90
#define DIFF_BITS 38
#define PTR_MASK ((1ULL << PTR_BITS) - 1)
#define SCND_PTR_MASK ((1ULL << SCND_PTR_BITS) - 1)
#define SIGN_EXTEND_SHIFT (64 - DIFF_BITS)
#define IS_FUNC 0xFFFFFFFFFFFFFFF8ULL
#define IS_THREAD_ID(x) ((x & ((uint128_t)-1 << 64)) && !(x & ((uint128_t)-1 >> 64)))

#define EXIT_FLAG ((uint128_t)1 << ((PTR_BITS) + 1))
#define MAX_PTHREAD_T (0xFFFFFFFFFFFFFFFFUL)
#define DATA_FLAG ((uint128_t)1 << (PTR_BITS))

#define second_ptr(t) (int64_t) ((t >> SECOND_PTR & SCND_PTR_MASK) << SCND_PTR_ALIGN_SHIFT)
#define third_ptr(t) \
    ({ \
        int64_t offset = ((int64_t)(t >> DIFF_START) << SIGN_EXTEND_SHIFT) >> SIGN_EXTEND_SHIFT; \
        offset ? (int64_t)((int64_t)(t & PTR_MASK) + offset) << PTR_ALIGN_SHIFT : 0; \
    })

#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

typedef struct {
    __uint128_t value;
    char padding[CACHE_LINE_SIZE - sizeof(__uint128_t)];
} CACHE_ALIGNED uint128_t_padded;

#define atomic_inc(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_RELAXED)
#define atomic_dec(ptr) __atomic_fetch_sub((ptr), 1, __ATOMIC_RELAXED)

static inline int __atomic128_cas(
    __uint128_t *address,
    __uint128_t *expected,
    __uint128_t desired
) {
    int success;
    uint64_t *exp_lo = (uint64_t *)expected;
    uint64_t *exp_hi = exp_lo + 1;
    __asm__ __volatile__ (
        "lock; cmpxchg16b %0"
        : "+m" (*address),
          "+d" (*exp_hi),
          "+a" (*exp_lo),
          "=@ccz" (success)
        : "c" ((uint64_t)(desired >> 64)),
          "b" ((uint64_t)desired)
        : "cc", "memory"
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
        : "=m" (*address)  // output is the memory location
        : "x" (value)      // input is the value in register
        : "memory"
    );
}

#define atomic128_store(ptr, val) \
    __atomic128_store(((__uint128_t*)ptr), val)

typedef void (*TaskFunc)(void *data);
typedef void (*TaskDestroy)(void *data);

#define thread_pool_join(thread_id) pthread_join(thread_id, NULL)

uint8_t thread_pool_schedule_task(TaskFunc func, void *data, TaskDestroy destroy, uint128_t should_exit);
#define thread_pool_run(func, data, destroy) thread_pool_schedule_task(func, data, destroy, 0)

void* worker_thread(void* arg);
#define thread_pool_new_thread() pthread_create(&(pthread_t){0}, NULL, worker_thread, NULL)
uint64_t thread_pool_spawn_joinable(TaskFunc task, void *task_user_data, TaskDestroy task_destroy);
void thread_pool_join_all();

int thread_pool_num_cores();

#endif
