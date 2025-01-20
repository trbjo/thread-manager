#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include "thread-manager.h"

static atomic_ulong __attribute__((aligned(CACHE_LINE_SIZE))) assigned = 0;
static atomic_ulong __attribute__((aligned(CACHE_LINE_SIZE))) scheduled = 0;
static uint128_t_padded slots[MAX_SLOTS] __attribute__((aligned(CACHE_LINE_SIZE)));
static pthread_t* threads;


void thread_pool_run(TaskFunc func, void* data, TaskDestroy destroy) {
    int64_t data_diff = ((uint64_t)data - (uint64_t)func) >> PTR_ALIGN_SHIFT;
    int64_t destroy_diff = ((uint64_t)destroy - (uint64_t)func) >> PTR_ALIGN_SHIFT;
    uint128_t packed = ((uint128_t)(data_diff & DIFF_MASK) << DATA_SHIFT) |
                      ((uint128_t)(destroy_diff & DIFF_MASK) << DESTROY_SHIFT) |
                      (((uint64_t)func >> PTR_ALIGN_SHIFT) & FUNC_MASK);

    uint8_t chunk = atomic_inc(&scheduled);
    uint128_t old = load_lo(&slots[chunk]);
    do {
        if (old == SHUTDOWN) return;
        if (old) syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        old = old == DONE;
    } while (!atomic128_cas(&slots[chunk], &old, packed));
}

static void* worker_thread(void* arg) {
    uint8_t chunk = atomic_inc(&assigned);
    while (1) {
        uint128_t task = atomic128_load(&slots[chunk]);
        if (task == SHUTDOWN) return NULL;

        if (task > DONE && atomic128_cas(&slots[chunk], &task, EMPTY)) {
            uint64_t func_addr = (task & FUNC_MASK) << PTR_ALIGN_SHIFT;
            TaskFunc func = (TaskFunc)func_addr;
            TaskDestroy destroy = (TaskDestroy)(func_addr + (SIGN_EXTEND((task >> DESTROY_SHIFT) & DIFF_MASK) << PTR_ALIGN_SHIFT));
            void* data = (void*)(func_addr + (SIGN_EXTEND((task >> DATA_SHIFT) & DIFF_MASK) << PTR_ALIGN_SHIFT));

            func(data);
            if (destroy) destroy(data);
            chunk = atomic_inc(&assigned);
        } else if (task == EMPTY && atomic128_cas(&slots[chunk], &task, DONE)) {
            syscall(SYS_futex, &slots[chunk], FUTEX_WAIT_PRIVATE, DONE, NULL, NULL, 0);
        }
    }
}

void thread_pool_initialize() {
    int n = sysconf(_SC_NPROCESSORS_CONF);
    threads = calloc(n + 1, sizeof(pthread_t));
    while (n--) pthread_create(++threads, NULL, worker_thread, NULL);
}

void thread_pool_destroy() {
    for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
        atomic128_store(&slots[chunk], SHUTDOWN);
        syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }

    while(*threads) pthread_join(*threads--, NULL);
    free(threads);
}
