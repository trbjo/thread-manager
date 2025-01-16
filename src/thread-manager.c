#include <stdlib.h>
#include <string.h>

#include "thread-manager.h"

static ThreadPool* g_pool;

static void* worker_thread(void* arg) {
    CacheAligned128_t* slots = g_pool->slots;
    atomic_ulong* next = &g_pool->next;
    uint16_t chunk = WRAP_SLOT(a_inc(next));

    while (1) {
        uint128_t old_value = atomic128_load(&slots[chunk].value);
        if (old_value == DONE) return NULL;

        if (old_value != EMPTY && atomic128_compare_exchange(&slots[chunk].value, &old_value, EMPTY)) {
            TaskFunc func = (TaskFunc)((uint64_t)old_value & VIRTUAL_ADDRESS_MASK);
            TaskDestroy destroy = (TaskDestroy)(func + (int16_t)((uint64_t)old_value >> 48));
            void* data = (void*)(uintptr_t)(old_value >> 64);

            func(data);
            if (destroy) destroy(data);
            chunk = WRAP_SLOT(a_inc(next));
        } else if (atomic128_compare_exchange(&slots[chunk].value, &old_value, DONE)) {
            syscall(SYS_futex, &slots[chunk].value, FUTEX_WAIT_PRIVATE, DONE, NULL, NULL, 0);
        }
    }
}

void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy) {
    uint64_t low = (uint64_t)task | ((uint64_t)(task_destroy - task) << 48);
    uint128_t new_value = (((uint128_t)(uint64_t)task_user_data) << 64) | low;

    const uint16_t chunk = WRAP_SLOT(a_inc(&g_pool->scheduled));

    uint128_t expected = atomic128_load(&g_pool->slots[chunk].value);
    do {
        expected = expected == DONE ? DONE : EMPTY;
    } while (!atomic128_compare_exchange(&g_pool->slots[chunk].value, &expected, new_value));

    if (expected == DONE) {
        syscall(SYS_futex, &g_pool->slots[chunk].value, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }
}

void thread_pool_initialize(void) {
    g_pool = aligned_alloc(CACHE_LINE_SIZE, sizeof(ThreadPool));
    if (!g_pool) abort();
    memset(g_pool, 0, sizeof(ThreadPool));

    g_pool->max_threads = sysconf(_SC_NPROCESSORS_CONF);
    g_pool->threads = malloc(sizeof(pthread_t) * g_pool->max_threads);

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_create(&g_pool->threads[i], NULL, worker_thread, NULL);
    }
}

void thread_pool_destroy(void) {
    uint64_t lowest = a_load(&g_pool->scheduled);
    while (lowest <= a_load(&g_pool->next)) {
        uint16_t chunk = WRAP_SLOT(lowest++);
        syscall(SYS_futex, &g_pool->slots[chunk].value, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_join(g_pool->threads[i], NULL);
    }

    free(g_pool->threads);
    free(g_pool);
}
