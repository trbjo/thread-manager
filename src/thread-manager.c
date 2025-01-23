#include <stdlib.h>
#include <pthread.h>
#include "thread-manager.h"

static atomic_ulong        ALIGNED assigned = 0;
static atomic_ulong        ALIGNED scheduled = 0;
static uint128_t_padded    ALIGNED slots[MAX_SLOTS];
static pthread_t           ALIGNED threads_array[MAX_THREADS];
static _Atomic(pthread_t*) ALIGNED threads = threads_array;

static void* worker_thread(void* arg) {
    uint8_t chunk = atomic_inc(&assigned);
    while (1) {
        uint128_t task = atomic128_load(&slots[chunk]);
        if (task == SHUTDOWN) return NULL;

        if (task > DONE && atomic128_cas(&slots[chunk], &task, EMPTY)) {
            int is_data_diff = task & 1;
            TaskFunc func = (TaskFunc)(uint64_t)((task >> 1 & PTR_MASK) << PTR_ALIGN_SHIFT);
            uint64_t second_ptr = ((task >> SECOND_PTR) & PTR_MASK) << PTR_ALIGN_SHIFT;
            uint64_t third_ptr = (uint64_t)func + (((int64_t)(task >> DIFF_START) << SIGN_EXTEND_SHIFT) >> (SIGN_EXTEND_SHIFT - PTR_ALIGN_SHIFT));

            void* data = (void*)(is_data_diff ? third_ptr : second_ptr);
            TaskDestroy destroy = (TaskDestroy)(is_data_diff ? second_ptr : third_ptr);

            func(data);
            if (destroy) destroy(data);
            chunk = atomic_inc(&assigned);
        } else if (task == EMPTY && atomic128_cas(&slots[chunk], &task, DONE)) {
            syscall(SYS_futex, &slots[chunk], FUTEX_WAIT_PRIVATE, DONE, NULL, NULL, 0);
        }
    }
}

void thread_pool_run(TaskFunc func, void* data, TaskDestroy destroy) {
    if (!func) return;

    int64_t data_diff = (data - (void*)func);
    int64_t destroy_diff = (destroy - func);
    int64_t is_data_diff = llabs(data_diff) < llabs(destroy_diff);

    uint128_t packed = is_data_diff;
    packed |= ((uint128_t)(uint64_t)func >> PTR_ALIGN_SHIFT) << 1;
    packed |= (uint128_t)(uint64_t)(is_data_diff ? destroy : data) >> PTR_ALIGN_SHIFT << SECOND_PTR;
    packed |= (uint128_t)(is_data_diff ? data_diff : destroy_diff) >> PTR_ALIGN_SHIFT << DIFF_START;

    uint8_t chunk = atomic_inc(&scheduled);
    uint128_t old = load_lo(&slots[chunk]);
    do {
        if (old == SHUTDOWN) return;
        if (old) syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        old = old == DONE;
    } while (!atomic128_cas(&slots[chunk], &old, packed));
}

int thread_pool_run_new_thread(TaskFunc func, void* data, TaskDestroy destroy) {
    thread_pool_run(func, data, destroy);
    pthread_create(atomic_ptr_inc(&threads), NULL, worker_thread, NULL);
    return atomic_load(&threads) - threads_array;
}

void thread_pool_initialize() {
    while (sysconf(_SC_NPROCESSORS_CONF) > thread_pool_run_new_thread(NULL, NULL, NULL));
}

void thread_pool_destroy() {
    for (int chunk = 0; chunk < MAX_SLOTS; chunk++) {
        atomic128_store(&slots[chunk], SHUTDOWN);
        syscall(SYS_futex, &slots[chunk], FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    }
    while (atomic_ptr_dec(&threads) > threads_array) pthread_join(*atomic_load(&threads), NULL);
}
