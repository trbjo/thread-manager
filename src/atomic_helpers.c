#include "atomic_helpers.h"

int is_locked(aligned_atomic_int* lock) {
    if (atomic_load(&lock->value)) {
        CPU_PAUSE();
        return 1;
    }
    return 0;
}

void spin_lock(aligned_atomic_int* lock) {
    while (atomic_exchange(&lock->value, 1)) {
        CPU_PAUSE();
    }
}

void spin_unlock(aligned_atomic_int* lock) {
    atomic_store(&lock->value, 0);
}
