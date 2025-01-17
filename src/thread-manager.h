#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define uint128_t __uint128_t
#define MAX_SLOTS 512
#define CACHE_LINE_SIZE 64
#define WRAP_SLOT(x) ((x) & (MAX_SLOTS - 1))
#define EMPTY 0
#define DONE 1

#define VIRTUAL_ADDRESS_MASK 0xFFFFFFFFFFFFULL

#define a_load(ptr)         atomic_load_explicit((ptr), memory_order_relaxed)
#define a_inc(ptr)          atomic_fetch_add_explicit((ptr), 1ULL, memory_order_relaxed)

typedef struct {
    uint128_t value;
    char padding[CACHE_LINE_SIZE - sizeof(uint128_t)];
} __attribute__((aligned(CACHE_LINE_SIZE))) uint128_t_padded;

static inline int __a128_cmp_exchange(
    uint128_t *address,
    uint128_t *expected,
    uint128_t desired
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

#define a128_cmp_exchange(ptr, expected, desired) \
    __a128_cmp_exchange(((uint128_t*)ptr), expected, desired)

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
    __atomic128_load(((uint128_t*)ptr))

static inline uint64_t __load_lo(uint128_t* ptr) {
    uint64_t result;
    __asm__ __volatile__(
        "movq %1, %0"
        : "=r" (result)
        : "m" (*ptr)
        : "memory"
    );
    return result;
}

#define load_lo(ptr) __load_lo(((uint128_t*)ptr))

typedef void (*TaskFunc)(void* user_data);
typedef void (*TaskDestroy)(void* data);

void thread_pool_initialize();
void thread_pool_destroy();
void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy);

#endif
