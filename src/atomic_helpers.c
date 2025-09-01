#include "atomic_helpers.h"
#include <stdlib.h>

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

aligned_atomic_int* aligned_atomic_int_new() {
    aligned_atomic_int* ptr;
    if (posix_memalign((void**)&ptr, CACHE_LINE_SIZE, sizeof(aligned_atomic_int)) != 0)
        return NULL;
    atomic_init(&ptr->value, 0);
    return ptr;
}

void aligned_atomic_int_free(aligned_atomic_int* ptr) {
    free(ptr);
}
