#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include <linux/futex.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "thread-manager.h"

static ThreadPool* g_pool = NULL;

static void* worker_thread(void* arg) {
    while (!atomic_load(&g_pool->shutdown.value)) {
        syscall(SYS_futex, &g_pool->push_counter.value, FUTEX_WAIT_PRIVATE, atomic_load(&g_pool->pull_counter.value), NULL, NULL, 0);
        uint32_t chunk = WRAP_SLOT(atomic_fetch_add(&g_pool->pull_counter.value, 1));
        unsigned long packed_data = atomic_exchange(&g_pool->task_data[chunk].value, 0);
        if (packed_data) {
            unsigned long packed_funcs = atomic_exchange(&g_pool->task_handles[chunk].value, 0);
            syscall(SYS_futex, &g_pool->task_handles[chunk].value, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

            TaskFunc task_func = (TaskFunc)(packed_funcs & VIRTUAL_ADDRESS_MASK);
            TaskDestroy destroy = (TaskDestroy)((uintptr_t)task_func + (int16_t)(packed_funcs >> 48));
            void* task_data = (void*)(uintptr_t)(packed_data & VIRTUAL_ADDRESS_MASK);
            task_func(task_data);
            if (destroy) {
                destroy(task_data);
            }
        } else {
            atomic_fetch_sub(&g_pool->pull_counter.value, 1);
        }
    }
    return NULL;
}

void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy) {
    uint32_t chunk = WRAP_SLOT(atomic_fetch_add(&g_pool->push_counter.value, 1));

    intptr_t destroy_offset = (intptr_t)task_destroy - (intptr_t)task;
    unsigned long packed_funcs = ((uintptr_t)task & VIRTUAL_ADDRESS_MASK) | (((unsigned long)(destroy_offset & 0xFFFF)) << 48);
    unsigned long packed_data = ((uintptr_t)task_user_data & VIRTUAL_ADDRESS_MASK);

    unsigned long old;
    while ((old = atomic_load(&g_pool->task_handles[chunk].value)) &&
           !syscall(SYS_futex, &g_pool->task_handles[chunk].value, FUTEX_WAIT_PRIVATE, old, NULL, NULL, 0));

    atomic_store(&g_pool->task_handles[chunk].value, packed_funcs);
    atomic_store(&g_pool->task_data[chunk].value, packed_data);
    syscall(SYS_futex, &g_pool->push_counter.value, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}

void thread_pool_initialize() {
    g_pool = calloc(1, sizeof(ThreadPool));
    if (!g_pool) return;

    g_pool->max_threads = sysconf(_SC_NPROCESSORS_CONF);
    g_pool->threads = malloc(sizeof(pthread_t) * g_pool->max_threads);

    atomic_init(&g_pool->shutdown.value, 0);
    atomic_init(&g_pool->pull_counter.value, 0);
    atomic_init(&g_pool->push_counter.value, 0);

    for (int i = 0; i < MAX_SLOTS; i++) {
        atomic_init(&g_pool->task_handles[i].value, 0);
        atomic_init(&g_pool->task_data[i].value, 0);
    }

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_create(&g_pool->threads[i], NULL, worker_thread, NULL);
    }
}

void thread_pool_destroy() {
    atomic_store(&g_pool->shutdown.value, 1);
    syscall(SYS_futex, &g_pool->push_counter.value, FUTEX_WAKE_PRIVATE, g_pool->max_threads, NULL, NULL, 0);

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_join(g_pool->threads[i], NULL);
    }

    free(g_pool->threads);
    free(g_pool);
}
