#include <stdlib.h>
#include <pthread.h>
#include "thread-manager-128.h"

#define MAX_SLOTS 256

static atomic_ulong     CACHE_ALIGNED assigned = 0;
static atomic_ulong     CACHE_ALIGNED scheduled = 0;
static uint128_t        CACHE_ALIGNED slots[MAX_SLOTS];

// Lower 6 bits (0-63) determine cache line
// Bits 6-7 (0-3) determine quarter within cache line
#define GET_SLOT(n) ({ \
    uint8_t _n = (n); \
    (uint128_t*)((char*)slots + ((_n & 0x3F) << 6) + (((_n >> 6) & 0x3) << 4)); \
})

void* worker_thread(void* arg) {
    const uint128_t thread_id = ((uint128_t)(uint64_t)pthread_self()) << 64;
    uint128_t* slot_ptr = GET_SLOT(atomic_inc(&assigned));

    while (1) {
        uint128_t task = atomic128_load(slot_ptr);
        if (IS_TASK(task) && atomic128_cas(slot_ptr, &task, task >= EXIT ? thread_id : EMPTY)) {
            intptr_t func_ptr = ((intptr_t)(task & PTR_MASK)) << PTR_ALIGN_SHIFT;
            intptr_t destroy_ptr = task & DATA_FLAG ? second_ptr(task) : third_ptr(task);
            void* data = (void*)(~task & DATA_FLAG ? second_ptr(task) : third_ptr(task));

            if (func_ptr) ((TaskFunc)func_ptr)(data);
            if (destroy_ptr) ((TaskDestroy)destroy_ptr)(data);

            if (task >= EXIT) return NULL;

            slot_ptr = GET_SLOT(atomic_inc(&assigned));
        } else if (task == EMPTY && atomic128_cas(slot_ptr, &task, SLEEPING)) {
            syscall(SYS_futex, slot_ptr, FUTEX_WAIT_PRIVATE, SLEEPING, NULL, NULL, 0);
        }
    }
}

uint128_t* thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy, bool should_exit) {
    int64_t data_diff = data ? (int64_t)((char*)data - (char*)func) : 0;
    int64_t destroy_diff = destroy ? (int64_t)((char*)destroy - (char*)func) : 0;
    int64_t is_data_diff = llabs(data_diff) < llabs(destroy_diff);

    uint128_t packed = ((uint128_t)(int64_t)func) >> PTR_ALIGN_SHIFT;
    packed |= (uint128_t)is_data_diff << PTR_BITS;
    packed |= ((uint128_t)(int64_t)(is_data_diff ? destroy : data) >> SCND_PTR_ALIGN_SHIFT) << SECOND_PTR;
    packed |= (uint128_t)(is_data_diff ? data_diff : destroy_diff) >> PTR_ALIGN_SHIFT << DIFF_START;
    packed &= ~EXIT;
    packed |= (uint128_t)should_exit << 127;

    uint128_t* slot_ptr = GET_SLOT(atomic_inc(&scheduled));
    uint128_t expected = atomic128_load(slot_ptr);

    do {
        expected = expected == SLEEPING; // exchange EMPTY (0) or SLEEPING (1) slots only
    } while (!atomic128_cas(slot_ptr, &expected, packed));

    if (expected) syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

    return slot_ptr;
}

uint64_t thread_pool_spawn_joinable(TaskFunc func, void* data, TaskDestroy destroy) {
    uint128_t* slot_ptr = thread_pool_schedule_task(func, data, destroy, 1);
    thread_pool_new_thread();

    while (1) {
        uint128_t val = atomic128_load(slot_ptr);
        if (IS_THREAD_ID(val) && atomic128_cas(slot_ptr, &val, EMPTY)) {
            return (val >> 64); // thread id is stored in the high bits
        }
        __builtin_ia32_pause();
    }
}

void thread_pool_join_all() {
    uint128_t mask;
    do {
        mask = EMPTY;
        for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
            uint128_t* slot_ptr = GET_SLOT(chunk);
            uint128_t value = (uint128_t)SLEEPING;

            if (atomic128_cas(slot_ptr, &value, EXIT)) {
                syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            } else if (IS_THREAD_ID(value) && atomic128_cas(slot_ptr, &value, EMPTY)) {
                thread_pool_join(value >> 64);
                value = EMPTY;
            }
            mask |= value;
        }
    } while (mask != EMPTY);

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
