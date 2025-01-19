#include <stdlib.h>
#include <pthread.h>
#include "thread-manager.h"

static atomic_ulong __attribute__((aligned(CACHE_LINE_SIZE))) assigned = 0;
static atomic_ulong __attribute__((aligned(CACHE_LINE_SIZE))) scheduled = 0;
static uint128_t_padded slots[MAX_SLOTS] __attribute__((aligned(CACHE_LINE_SIZE)));
static pthread_t* threads;

static void* worker_thread(void* arg) {
    uint8_t chunk = atomic_inc(&assigned);
    while (1) {
        uint128_t task = atomic128_load(&slots[chunk]);
        if (task == SHUTDOWN) return NULL;

        if (task > DONE && atomic128_cas(&slots[chunk], &task, EMPTY)) {
            TaskFunc func = (TaskFunc)((uint64_t)task & VIRTUAL_ADDRESS_MASK);
            TaskDestroy destroy = (TaskDestroy)(func + (int32_t)(task >> 48));
            void* data = (void*)(uint64_t)(task >> 80);

            func(data);
            if (destroy) destroy(data);
            chunk = atomic_inc(&assigned);
        } else if (task == EMPTY && atomic128_cas(&slots[chunk], &task, DONE)) {
            syscall(SYS_futex, &slots[chunk], FUTEX_WAIT_PRIVATE, DONE, NULL, NULL, 0);
        }
    }
}

void thread_pool_run(TaskFunc func, void* data, TaskDestroy destroy) {
    uint128_t packed = ((uint128_t)((uint64_t)data) << 80) |           // User data ptr in upper 48 bits
                       ((uint128_t)(uint32_t)(destroy - func)) << 48 | // TaskDestroy offset truncated to 32 bits to prevent sign extension
                       (uint64_t)func;                                 // TaskFunc in lower 48 bits

    uint8_t chunk = atomic_inc(&scheduled);
    uint128_t old = load_lo(&slots[chunk]);
    do {
        if (old == SHUTDOWN) return;
        if (old) syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        old = old == DONE; // exchange only if EMPTY (0) or DONE (1)
    } while (!atomic128_cas(&slots[chunk], &old, packed));
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
