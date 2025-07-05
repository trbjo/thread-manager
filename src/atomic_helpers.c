#include "atomic_helpers.h"

int is_locked(atomic_int* lock) {
    if (atomic_load(lock)) {
        CPU_PAUSE();
        return 1;
    }
    return 0;
}

void spin_lock(atomic_int* lock) {
    while (atomic_exchange(lock, 1)) {
        CPU_PAUSE();
    }
}

void spin_unlock(atomic_int* lock) {
    atomic_store(lock, 0);
}
