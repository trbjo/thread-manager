#include <stdlib.h>
#include <pthread.h>
#include "thread-manager-128.h"

static atomic_ulong     CACHE_ALIGNED assigned = 0;
static atomic_ulong     CACHE_ALIGNED scheduled = 0;
static uint128_t        CACHE_ALIGNED slots[MAX_SLOTS];

// Lower 6 bits (0-63) determine cache line
// Bits 6-7 (0-3) determine quarter within cache line
#define GET_SLOT(n) ((uint128_t*)((char*)slots + ((n & 0x3F) << 6) + (((n >> 6) & 0x3) << 4)))

#define GET_NEW_SLOT(counter_ptr) ({ \
    uint8_t _val = atomic_inc(counter_ptr); \
    (uint128_t*)((char*)slots + ((_val & 0x3F) << 6) + (((_val >> 6) & 0x3) << 4)); \
})

void* worker_thread(void* arg) {
    uint128_t thread_id = ((uint128_t)(uint64_t)pthread_self()) << 64;
    uint128_t* slot_ptr = GET_NEW_SLOT(&assigned);
    uint128_t task = atomic128_load(slot_ptr);

    while (1) {
        if ((task & IS_FUNC) && atomic128_cas(slot_ptr, &task, (task & EXIT_FLAG) ? thread_id : EMPTY)) {
            int64_t func_ptr = ((int64_t)(task & PTR_MASK)) << PTR_ALIGN_SHIFT;
            int64_t destroy_ptr = task & DATA_FLAG ? second_ptr(task) : third_ptr(task);
            void* data = (void*)(~task & DATA_FLAG ? second_ptr(task) : third_ptr(task));

            if (func_ptr) ((TaskFunc)(intptr_t)func_ptr)(data);
            if (destroy_ptr) ((TaskDestroy)(intptr_t)destroy_ptr)(data);
            if (task & EXIT_FLAG) return NULL;

            slot_ptr = GET_NEW_SLOT(&assigned);
            task = atomic128_load(slot_ptr);
        } else if (task == EMPTY && atomic128_cas(slot_ptr, &task, SLEEPING)) {
            syscall(SYS_futex, slot_ptr, FUTEX_WAIT_PRIVATE, SLEEPING, NULL, NULL, 0);
            task = atomic128_load(slot_ptr);
        }
    }
}

uint128_t* thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy, uint128_t should_exit) {
    int64_t data_diff = (data == NULL) ? 0 : (int64_t)((char*)data - (char*)func);
    int64_t destroy_diff = (destroy == NULL) ? 0 : (int64_t)((char*)destroy - (char*)func);
    int64_t is_data_diff = llabs(data_diff) < llabs(destroy_diff);

    uint128_t packed = ((uint128_t)(int64_t)func) >> PTR_ALIGN_SHIFT;
    packed |= (uint128_t)is_data_diff << PTR_BITS | (uint128_t)should_exit << (PTR_BITS + 1);
    packed |= (((uint128_t)(int64_t)(is_data_diff ? destroy : data)) >> SCND_PTR_ALIGN_SHIFT) << SECOND_PTR;
    packed |= (((uint128_t)(int64_t)(is_data_diff ? data_diff : destroy_diff)) >> PTR_ALIGN_SHIFT) << DIFF_START;

    uint128_t* slot_ptr = GET_NEW_SLOT(&scheduled);
    uint128_t expected = atomic128_load(slot_ptr);

    do {
        expected = expected == SLEEPING; // exchange EMPTY (0) or SLEEPING (1) slots only
    } while (!atomic128_cas(slot_ptr, &expected, packed));
    if (expected) syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

    return slot_ptr;
}

uint64_t thread_pool_spawn_joinable(TaskFunc func, void* data, TaskDestroy destroy) {
    thread_pool_new_thread();
    uint128_t* slot_ptr = thread_pool_schedule_task(func, data, destroy, 1);
    while (!IS_THREAD_ID(atomic128_load(slot_ptr))) __builtin_ia32_pause();

    return atomic_exchange((uint64_t*)((char*)slot_ptr + 8), EMPTY);
}

void thread_pool_join_all() {
    uint128_t mask = EMPTY;
    while (mask != EXIT_FLAG) {
        mask = EMPTY;
        for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
            uint128_t *slot_ptr = GET_SLOT(chunk);
            uint128_t value = (uint128_t)SLEEPING;

            if (atomic128_cas(slot_ptr, &value, EXIT_FLAG)) {
                syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            } else if (IS_THREAD_ID(value) && atomic128_cas(slot_ptr, &value, EXIT_FLAG)) {
                thread_pool_join(value >> 64);
            }
            mask |= value;
        }
    }

    for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
        atomic128_store(GET_SLOT(chunk), EMPTY);
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
