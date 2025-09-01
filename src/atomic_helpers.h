#ifndef ATOMIC_HELPERS_H
#define ATOMIC_HELPERS_H

#include <stdatomic.h>

#define CACHE_LINE_SIZE 64

#ifdef __aarch64__
    #define CPU_PAUSE() asm volatile("yield" ::: "memory")
#else
    #define CPU_PAUSE() __builtin_ia32_pause()
#endif

typedef struct {
    alignas(CACHE_LINE_SIZE) atomic_int value;
    char padding[CACHE_LINE_SIZE - sizeof(atomic_int)];
} aligned_atomic_int;

aligned_atomic_int* aligned_atomic_int_new(void);
void aligned_atomic_int_free(aligned_atomic_int* ptr);


int is_locked(aligned_atomic_int *lock);
void spin_lock(aligned_atomic_int *lock);
void spin_unlock(aligned_atomic_int *lock);

#endif
