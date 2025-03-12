#include <stdlib.h>
#include <pthread.h>
#include "thread-manager-128.h"

static atomic_ulong     CACHE_ALIGNED assigned = 0;
static atomic_ulong     CACHE_ALIGNED scheduled = 0;
static uint128_t_padded CACHE_ALIGNED slots[MAX_SLOTS];

void* worker_thread(void* arg) {
    uint8_t chunk = atomic_inc(&assigned);
    uint128_t thread_id = ((uint128_t)pthread_self()) << 64;
    while (1) {
        uint128_t task = atomic128_load(&slots[chunk]);
        if (task & IS_FUNC && atomic128_cas(&slots[chunk], &task, (task & EXIT_FLAG) ? thread_id : EMPTY)) {
            int64_t func_ptr = (task & PTR_MASK) << PTR_ALIGN_SHIFT;
            int64_t destroy_ptr = task & DATA_FLAG ? second_ptr(task) : third_ptr(task);
            void* data = (void*) (~task & DATA_FLAG ? second_ptr(task) : third_ptr(task));

            if (func_ptr) ((TaskFunc)func_ptr)(data);
            if (destroy_ptr) ((TaskDestroy)destroy_ptr)(data);
            if (task & EXIT_FLAG) return NULL;

            chunk = atomic_inc(&assigned);
        } else if (task == EMPTY && atomic128_cas(&slots[chunk], &task, SLEEPING)) {
            syscall(SYS_futex, &slots[chunk], FUTEX_WAIT_PRIVATE, SLEEPING, NULL, NULL, 0);
        }
    }
}

uint8_t thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy, uint128_t should_exit) {
    int64_t data_diff = (data == NULL) ? 0 : (data - (void*)func);
    int64_t destroy_diff = (destroy == NULL) ? 0 : (destroy - func);
    int64_t is_data_diff = llabs(data_diff) < llabs(destroy_diff);

    uint128_t packed = (uint128_t)(int64_t)func >> PTR_ALIGN_SHIFT;
    packed |= is_data_diff << PTR_BITS | should_exit << (PTR_BITS + 1);
    packed |= (uint128_t)(int64_t)(is_data_diff ? destroy : data) >> SCND_PTR_ALIGN_SHIFT << SECOND_PTR;
    packed |= (uint128_t)(int64_t)(is_data_diff ? data_diff : destroy_diff) >> PTR_ALIGN_SHIFT << DIFF_START;

    uint8_t chunk = atomic_inc(&scheduled);
    uint128_t expected = atomic128_load(&slots[chunk]);
    do {
        if (expected) syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        expected = expected == SLEEPING; // exchange EMPTY (0) or SLEEPING (1) slots only
    } while (!atomic128_cas(&slots[chunk], &expected, packed));
    return chunk;
}

uint64_t thread_pool_spawn_joinable(TaskFunc func, void* data, TaskDestroy destroy) {
    uint8_t chunk = thread_pool_schedule_task(func, data, destroy, 1);
    thread_pool_new_thread();
    while (!IS_THREAD_ID(atomic128_load(&slots[chunk]))) __builtin_ia32_pause();
    return atomic_exchange((uint64_t*)((char*)&slots[chunk] + 8), EMPTY);
}

void thread_pool_join_all() {
    uint128_t mask = EMPTY;
    while (mask != EXIT_FLAG) {
        mask = EMPTY;
        for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
            uint128_t value = (uint128_t)SLEEPING;
            if (atomic128_cas(&slots[chunk], &value, EXIT_FLAG)) {
                syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            } else if (IS_THREAD_ID(value) && atomic128_cas(&slots[chunk], &value, EXIT_FLAG)) {
                thread_pool_join(value >> 64);
            }
            mask |= value;
        }
    }
    for (int chunk = 0; chunk < MAX_SLOTS; chunk++) atomic128_store(&slots[chunk], EMPTY);
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

