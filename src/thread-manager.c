#include <stdlib.h>
#include <pthread.h>
#include "thread-manager.h"

static pthread_t* threads;
static atomic_ulong __attribute__((aligned(CACHE_LINE_SIZE))) assigned = 0;
static atomic_ulong __attribute__((aligned(CACHE_LINE_SIZE))) scheduled = 0;
static uint128_t_padded slots[MAX_SLOTS] __attribute__((aligned(CACHE_LINE_SIZE)));

static void* worker_thread(void* arg) {
    uint16_t chunk = WRAP_SLOT(a_inc(&assigned));
    while (1) {
        uint128_t task = atomic128_load(&slots[chunk]);
        if (task == DONE) return NULL;

        if (task != EMPTY && a128_cmp_exchange(&slots[chunk], &task, EMPTY)) {
            TaskFunc func = (TaskFunc)((uintptr_t)task & VIRTUAL_ADDRESS_MASK);
            TaskDestroy destroy = (TaskDestroy)(func + (int16_t)((uintptr_t)task >> 48));
            void* data = (void*)(uintptr_t)(task >> 64);

            func(data);
            if (destroy) destroy(data);
            chunk = WRAP_SLOT(a_inc(&assigned));
        } else if (task == EMPTY && a128_cmp_exchange(&slots[chunk], &task, DONE)) {
            syscall(SYS_futex, &slots[chunk], FUTEX_WAIT_PRIVATE, DONE, NULL, NULL, 0);
        }
    }
}

void thread_pool_run(TaskFunc func, void* data, TaskDestroy destroy) {
    uint128_t new_value = ((uint128_t)(uintptr_t)data << 64) | ((uintptr_t)(destroy - func) << 48) | ((uintptr_t)func);

    const uint16_t chunk = WRAP_SLOT(a_inc(&scheduled));

    uint128_t expected = load_lo(&slots[chunk]) == DONE ? DONE : EMPTY;
    while (!a128_cmp_exchange(&slots[chunk], &expected, new_value)) {
        expected = expected == DONE ? DONE : EMPTY;
    }

    if (expected == DONE) {
        syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }
}

void thread_pool_initialize() {
    const int num_threads = sysconf(_SC_NPROCESSORS_CONF);
    threads = malloc(num_threads * sizeof(pthread_t));

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }
}

void thread_pool_destroy() {
    uint64_t lowest = a_load(&scheduled);
    while (lowest <= a_load(&assigned)) {
        uint16_t chunk = WRAP_SLOT(lowest++);
        syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }

    for (int i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
}
