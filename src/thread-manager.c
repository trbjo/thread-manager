#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "thread-manager.h"

static ThreadPool* g_pool;

static void* worker_thread(void* arg) {
    padded_atomic_ulong* slots = g_pool->slots;
    atomic_ulong* next = &g_pool->next.value;
    uint16_t chunk = WRAP_SLOT(a_inc(next));

    while (1) {
        uint64_t ptr = a_exchange(&slots[chunk].value, EMPTY);
        if (ptr == IDLE) return NULL;
        if (ptr) {
            TaskSlot* slot = (TaskSlot*)ptr;
            TaskFunc task_func = (TaskFunc)(slot->funcs & VIRTUAL_ADDRESS_MASK);
            TaskDestroy destroy = (TaskDestroy)((char*)task_func + (int16_t)(slot->funcs >> 48));
            void* task_data = (void*)(slot->data & VIRTUAL_ADDRESS_MASK);

            task_func(task_data);
            if (destroy) destroy(task_data);
            free(slot);
            chunk = WRAP_SLOT(a_inc(next));
        } else if (a_cmp_exchange(&slots[chunk].value, &ptr, IDLE)) {
            syscall(SYS_futex, &slots[chunk].value, FUTEX_WAIT_PRIVATE, IDLE, NULL, NULL, 0);
        }
    }
}

void thread_pool_run(TaskFunc task, void* task_user_data, TaskDestroy task_destroy) {
    TaskSlot* slot = malloc(sizeof(TaskSlot));
    slot->funcs = ((uint64_t)task & VIRTUAL_ADDRESS_MASK) | ((uint64_t)((char*)task_destroy - (char*)task) << 48);
    slot->data = (uint64_t)task_user_data & VIRTUAL_ADDRESS_MASK;

    const uint16_t chunk = WRAP_SLOT(a_inc(&g_pool->scheduled.value));

    uint64_t old = EMPTY;
    while (!a_cmp_exchange(&g_pool->slots[chunk].value, &old, (uint64_t)slot)) {
        if (old > IDLE) {
            old = EMPTY;
        }
    }

    if (old == IDLE) {
        syscall(SYS_futex, &g_pool->slots[chunk].value, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }
}

void thread_pool_initialize() {
    g_pool = calloc(1, sizeof(ThreadPool));
    if (!g_pool) return;

    g_pool->max_threads = sysconf(_SC_NPROCESSORS_CONF);
    g_pool->threads = malloc(sizeof(pthread_t) * g_pool->max_threads);

    atomic_init(&g_pool->next.value, 0);
    atomic_init(&g_pool->scheduled.value, 0);

    for (int i = 0; i < MAX_SLOTS; i++) {
        atomic_init(&g_pool->slots[i].value, 0);
    }

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_create(&g_pool->threads[i], NULL, worker_thread, NULL);
    }
}

void thread_pool_destroy() {
    uint64_t lowest = a_load(&g_pool->scheduled.value);
    while (lowest <= a_load(&g_pool->next.value)) {
        uint16_t chunk = WRAP_SLOT(lowest++);
        syscall(SYS_futex, &g_pool->slots[chunk].value, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }

    for (int i = 0; i < g_pool->max_threads; i++) {
        pthread_join(g_pool->threads[i], NULL);
    }

    free(g_pool->threads);
    free(g_pool);
}
