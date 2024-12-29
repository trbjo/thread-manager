#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "thread-manager.h"

static ThreadPool* g_thread_pool = NULL;

static void* worker_thread(void* arg) {
    const uint8_t base_chunk = (uintptr_t)arg >> 52;
    ThreadPool* pool = (ThreadPool*)((uintptr_t)arg & ((1ULL << 48) - 1));
    uint8_t chunk = base_chunk % CHUNKS;

    while (!atomic_load(&pool->shutdown)) {
        unsigned long old = atomic_load(&pool->ready[chunk].value);
        if (old && atomic_compare_exchange_strong(&pool->ready[chunk].value, &old, CLEAR_LOWEST_BIT(old))) {
            atomic_fetch_sub(&pool->tasks, 1);
            int bit = LOWEST_SET_BIT(old);
            int slot = bit + (chunk * BITS_PER_CHUNK);

            TaskFunc task_func = pool->task_handles[slot].task;
            void* task_data = pool->task_handles[slot].task_user_data;
            TaskDestroy task_destroy = pool->task_handles[slot].task_destroy;

            atomic_fetch_and(&pool->claimed[chunk].value, UNSET_BIT_MASK(bit));

            task_func(task_data);
            if (task_destroy) {
                task_destroy(task_data);
            }
        } else if (syscall(SYS_futex, &pool->tasks, FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0)) { // more work, just not in this trunk
            __builtin_ia32_pause();
            chunk = (base_chunk + chunk + 1) % CHUNKS;
        } else { // futex slept, reset to base chunk
            chunk = atomic_load(&pool->current_chunk) % CHUNKS;
        }
    }
    return NULL;
}

void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy) {
    atomic_fetch_add(&g_thread_pool->tasks, 1);
    while (!atomic_load(&g_thread_pool->shutdown)) {
        uint8_t chunk = atomic_fetch_add(&g_thread_pool->current_chunk, 1) % CHUNKS;
        unsigned long old = atomic_load(&g_thread_pool->claimed[chunk].value);

        if (old != CHUNK_FULL) {
            int bit = LOWEST_UNSET_BIT(old);
            unsigned long free_bit_position = 1UL << bit;
            unsigned long new = old | free_bit_position;

            if (atomic_compare_exchange_strong(&g_thread_pool->claimed[chunk].value, &old, new)) {
                syscall(SYS_futex, &g_thread_pool->tasks, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
                int slot = bit + (chunk * BITS_PER_CHUNK);

                g_thread_pool->task_handles[slot].task = task;
                g_thread_pool->task_handles[slot].task_user_data = task_user_data;
                g_thread_pool->task_handles[slot].task_destroy = task_destroy;

                atomic_fetch_or(&g_thread_pool->ready[chunk].value, free_bit_position);
                return;
            }
        }
        __builtin_ia32_pause();
    }
}

static ThreadPool* thread_pool_create() {
    ThreadPool* pool = calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;

    pool->max_threads = sysconf(_SC_NPROCESSORS_CONF);
    pool->threads = malloc(sizeof(pthread_t) * pool->max_threads);
    pool->task_handles = calloc(MAX_SLOTS, sizeof(TaskHandle));

    atomic_init(&pool->shutdown, 0);
    atomic_init(&pool->tasks, 0);
    atomic_init(&pool->current_chunk, 0);

    for (int i = 0; i < CHUNKS; i++) {
        atomic_init(&pool->ready[i].value, 0);
        atomic_init(&pool->claimed[i].value, 0);
    }

    uintptr_t base_ptr = (uintptr_t)pool;

    for (uint8_t i = 0; i < pool->max_threads; i++) {
        uintptr_t context = base_ptr | ((uintptr_t)i << 52);
        pthread_create(&pool->threads[i], NULL, worker_thread, (void*)context);
    }

    return pool;
}

void thread_pool_initialize() {
    g_thread_pool = thread_pool_create();
}

void thread_pool_destroy() {
    atomic_store(&g_thread_pool->shutdown, 1);
    syscall(SYS_futex, &g_thread_pool->tasks, FUTEX_WAKE_PRIVATE, g_thread_pool->max_threads, NULL, NULL, 0);

    for (int i = 0; i < g_thread_pool->max_threads; i++) {
        pthread_join(g_thread_pool->threads[i], NULL);
    }

    free(g_thread_pool->task_handles);
    free(g_thread_pool->threads);
    free(g_thread_pool);
}
