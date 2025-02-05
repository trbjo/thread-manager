#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define uint128_t __uint128_t
#define MAX_SLOTS (((uint8_t)~0) + 1)
#define CACHE_LINE_SIZE 64
#define EMPTY 0
#define DONE 1
#define SHUTDOWN 4

#define PTR_ALIGN_SHIFT 4
#define PTR_BITS 43
#define SECOND_PTR 44
#define DIFF_START 87
#define DIFF_BITS 41
#define MAX_THREADS 32
#define ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

#define PTR_MASK ((1ULL << PTR_BITS) - 1)
#define SIGN_EXTEND_SHIFT (64 - DIFF_BITS)

typedef struct {
    __uint128_t value;
    char padding[CACHE_LINE_SIZE - sizeof(__uint128_t)];
} __attribute__((aligned(CACHE_LINE_SIZE))) uint128_t_padded;

#define atomic_inc(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_RELAXED)
#define atomic_ptr_dec(ptr) __atomic_fetch_sub((ptr), sizeof(pthread_t), __ATOMIC_RELAXED)
#define atomic_ptr_inc(ptr) __atomic_fetch_add((ptr), sizeof(pthread_t), __ATOMIC_RELAXED)

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
        : "=m" (*address)
        : "x" (value)
        : "memory"
    );
}

#define atomic128_store(ptr, value) \
    __atomic128_store((__uint128_t*)(ptr), value)

static inline uint64_t __load_lo(__uint128_t* ptr) {
    uint64_t result;
    __asm__ __volatile__(
        "movq %1, %0"
        : "=r" (result)
        : "m" (*ptr)
        : "memory"
    );
    return result;
}

#define load_lo(ptr) __load_lo(((__uint128_t*)ptr))

typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

void thread_pool_initialize();
void thread_pool_destroy();
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);
int thread_pool_run_new_thread(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

#endif
