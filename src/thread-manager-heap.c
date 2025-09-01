#include <stdlib.h>
#include <pthread.h>
#include "thread-manager-heap.h"

static atomic_ulong     CACHE_ALIGNED assigned = 0;
static atomic_ulong     CACHE_ALIGNED scheduled = 0;
static atomic_ulong     CACHE_ALIGNED slots[MAX_SLOTS];

// 8 slots per cache line for 64-bit values
// Lower 5 bits (0-31) determine cache line
// Bits 5-7 (0-7) determine position within cache line
#define GET_SLOT(n) ((atomic_ulong*)((char*)slots + ((n & 0x1F) << 6) + (((n >> 5) & 0x7) << 3)))

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
        atomic_ulong* slot_ptr = GET_SLOT(chunk);
        uint64_t slot = a_load(slot_ptr);

        if ((slot & TASK_OR_EXIT_FLAG) && a_compare_exchange_weak(slot_ptr, &slot, (slot & EXIT_FLAG) ? thread_id : EMPTY)) {
            task_data* task = (task_data*)(slot >> PTR_SHIFT);
            if (task) {
                if (task->func) task->func(task->data);
                if (task->destroy) task->destroy(task->data);
                free(task);
            }
            if (slot & EXIT_FLAG) return NULL;
            chunk = atomic_inc(&assigned);
        } else if (slot == EMPTY && atomic_compare_exchange_weak(slot_ptr, &slot, SLEEPING)) {
            syscall(SYS_futex, slot_ptr, FUTEX_WAIT_PRIVATE, SLEEPING, NULL, NULL, 0);
        }
    }
}

uint16_t thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy, uint64_t should_exit) {
    task_data* task = create_task(func, data, destroy);
    if (!task) return 0;

    uint16_t chunk = atomic_inc(&scheduled);
    uint64_t task_ptr = ((uint64_t)task << PTR_SHIFT) | TASK_FLAG;
    if (should_exit) task_ptr |= EXIT_FLAG;

    atomic_ulong* slot_ptr = GET_SLOT(chunk);
    uint64_t expected = a_load(slot_ptr);
    do {
        if (expected == SLEEPING) syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        expected = expected == SLEEPING; // exchange EMPTY (0) or SLEEPING (1) slots only
    } while (!a_compare_exchange_weak(slot_ptr, &expected, task_ptr));

    return chunk;
}

uint64_t thread_pool_spawn_joinable(TaskFunc func, void* data, TaskDestroy destroy) {
    thread_pool_new_thread();
    uint16_t chunk = thread_pool_schedule_task(func, data, destroy, 1);
    atomic_ulong* slot_ptr = GET_SLOT(chunk);
    while (!IS_THREAD_ID(a_load(slot_ptr))) CPU_PAUSE();
    return a_exchange(slot_ptr, EMPTY) >> PTR_SHIFT;
}

void thread_pool_join_all() {
    uint64_t mask = EMPTY;
    while (mask != EXIT_FLAG) {
        mask = EMPTY;
        for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
            atomic_ulong* slot_ptr = GET_SLOT(chunk);
            uint64_t value = SLEEPING;
            if (a_compare_exchange_strong(slot_ptr, &value, EXIT_FLAG)) {
                syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            } else if (IS_THREAD_ID(value) && a_compare_exchange_strong(slot_ptr, &value, EXIT_FLAG)) {
                thread_pool_join(value >> PTR_SHIFT);
            }
            mask |= value;
        }
    }
    for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
        atomic_ulong* slot_ptr = GET_SLOT(chunk);
        a_store(slot_ptr, EMPTY);
    }
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
