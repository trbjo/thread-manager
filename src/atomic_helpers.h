#ifndef ATOMIC_HELPERS_H
#define ATOMIC_HELPERS_H

#include <stdatomic.h>

#ifdef __aarch64__
    #define CPU_PAUSE() asm volatile("yield" ::: "memory")
#else
    #define CPU_PAUSE() __builtin_ia32_pause()
#endif


int is_locked(atomic_int *lock);
void spin_lock(atomic_int *lock);
void spin_unlock(atomic_int *lock);

#endif
