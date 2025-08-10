#include <stdlib.h>
#include <pthread.h>
#include "thread-manager-heap.h"

static atomic_ulong     CACHE_ALIGNED assigned = 0;
static atomic_ulong     CACHE_ALIGNED scheduled = 0;
static atomic_ulong_padded CACHE_ALIGNED slots[MAX_SLOTS];

typedef struct task_data {
    TaskFunc func;
    TaskDestroy destroy;
    void* data;
} task_data;

static task_data* create_task(TaskFunc func, void* data, TaskDestroy destroy) {
    task_data* task = calloc(1, sizeof(task_data));
    if (!task) return NULL;
    task->func = func;
    task->data = data;
    task->destroy = destroy;
    return task;
}

void* worker_thread(void* arg) {
    uint16_t chunk = atomic_inc(&assigned);
    uint64_t thread_id = (uint64_t)pthread_self() << PTR_SHIFT;

    while (1) {
        uint64_t slot = a_load(&slots[chunk].value);
        if ((slot & TASK_OR_EXIT_FLAG) && a_compare_exchange_strong(&slots[chunk], &slot, (slot & EXIT_FLAG) ? thread_id : EMPTY)) {
            task_data* task = (task_data*)(slot >> PTR_SHIFT);
            if (task) {
                if (task->func) task->func(task->data);
                if (task->destroy) task->destroy(task->data);
                free(task);
            }
            if (slot & EXIT_FLAG) return NULL;
            chunk = atomic_inc(&assigned);
        } else if (slot == EMPTY && a_compare_exchange_strong(&slots[chunk], &slot, SLEEPING)) {
            syscall(SYS_futex, &slots[chunk], FUTEX_WAIT_PRIVATE, SLEEPING, NULL, NULL, 0);
        }
    }
}

uint16_t thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy, uint64_t should_exit) {
    task_data* task = create_task(func, data, destroy);
    if (!task) return 0;

    uint16_t chunk = atomic_inc(&scheduled);
    uint64_t task_ptr = ((uint64_t)task << PTR_SHIFT) | TASK_FLAG;
    if (should_exit) task_ptr |= EXIT_FLAG;

    uint64_t expected = a_load(&slots[chunk]);
    do {
        if (expected == SLEEPING) syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        expected = expected == SLEEPING; // exchange EMPTY (0) or SLEEPING (1) slots only
    } while (!a_compare_exchange_weak(&slots[chunk], &expected, task_ptr));

    return chunk;
}

uint64_t thread_pool_spawn_joinable(TaskFunc func, void* data, TaskDestroy destroy) {
    thread_pool_new_thread();
    uint16_t chunk = thread_pool_schedule_task(func, data, destroy, 1);
    while (!IS_THREAD_ID(a_load(&slots[chunk]))) CPU_PAUSE();
    return a_exchange(&slots[chunk], EMPTY) >> PTR_SHIFT;
}

void thread_pool_join_all() {
    uint64_t mask = EMPTY;
    while (mask != EXIT_FLAG) {
        mask = EMPTY;
        for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
            uint64_t value = SLEEPING;
            if (a_compare_exchange_strong(&slots[chunk], &value, EXIT_FLAG)) {
                syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            } else if (IS_THREAD_ID(value) && a_compare_exchange_strong(&slots[chunk], &value, EXIT_FLAG)) {
                thread_pool_join(value >> PTR_SHIFT);
            }
            mask |= value;
        }
    }
    for (int chunk = 0; chunk < MAX_SLOTS; chunk++) a_store(&slots[chunk], EMPTY);
    atomic_store(&scheduled, 0);
    atomic_store(&assigned, 0);
}

static int cores;

int thread_pool_num_cores() {
    if (cores == 0) {
        cores = sysconf(_SC_NPROCESSORS_CONF);
    }
    return cores;
}
