#include <stdlib.h>

#include "thread-manager.h"

static ThreadPool* g_pool;

static void* worker_thread(void* arg) {
    atomic128_t* slots = g_pool->slots;
    atomic_ulong* next = &g_pool->next;
    uint16_t chunk = WRAP_SLOT(a_inc(next));

    while (1) {
        uint64_t old_high, old_low;
        atomic128_exchange(&slots[chunk], &old_high, &old_low, EMPTY, EMPTY);

        if (old_high == DONE) return NULL;
        if (old_low != EMPTY) {
            TaskFunc func = (TaskFunc)(old_low & VIRTUAL_ADDRESS_MASK);
            TaskDestroy destroy = (TaskDestroy)(func + (int16_t)(old_low >> 48));
            void* data = (void*)old_high;

            func(data);
            if (destroy) destroy(data);
            chunk = WRAP_SLOT(a_inc(next));
        } else if (a_cmp_exchange(&slots[chunk].high, &old_high, DONE)) {
            syscall(SYS_futex, &slots[chunk].high, FUTEX_WAIT_PRIVATE, DONE, NULL, NULL, 0);
        }
    }
}

void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy) {
    uint64_t low = (uint64_t)task | ((uint64_t)(task_destroy - task) << 48);
    uint64_t high = (uint64_t)task_user_data;

    const uint16_t chunk = WRAP_SLOT(a_inc(&g_pool->scheduled));

    uint64_t expected_low, expected_high;
    do {
        expected_high = g_pool->slots[chunk].high;
        expected_low = EMPTY;
    } while (!atomic128_compare_exchange(&g_pool->slots[chunk], &expected_high, &expected_low, high, low));

    if (expected_high == DONE) {
        syscall(SYS_futex, &g_pool->slots[chunk].high, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }
}

void thread_pool_initialize(void) {
    g_pool = aligned_alloc(CACHE_LINE_SIZE, sizeof(ThreadPool));
    if (!g_pool) return;

    g_pool->max_threads = sysconf(_SC_NPROCESSORS_CONF);
    g_pool->threads = malloc(sizeof(pthread_t) * g_pool->max_threads);

    atomic_init(&g_pool->next, 0);
    atomic_init(&g_pool->scheduled, 0);

    const atomic128_t EMPTY_SLOT = {.low = 0ULL, .high = 0ULL};
    for (int i = 0; i < MAX_SLOTS; i++) {
        g_pool->slots[i] = EMPTY_SLOT;
    }

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_create(&g_pool->threads[i], NULL, worker_thread, NULL);
    }
}

void thread_pool_destroy(void) {
    uint64_t lowest = a_load(&g_pool->scheduled);
    while (lowest <= a_load(&g_pool->next)) {
        uint16_t chunk = WRAP_SLOT(lowest++);
        syscall(SYS_futex, &g_pool->slots[chunk].high, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_join(g_pool->threads[i], NULL);
    }

    free(g_pool->threads);
    free(g_pool);
}
