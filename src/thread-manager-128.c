#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "thread-manager-128.h"

#define MAX_SLOTS 65536

static int              *core_ids;
static pthread_t        *worker_tids;

static atomic_ulong     CACHE_ALIGNED assigned = 0;
static atomic_ulong     CACHE_ALIGNED scheduled = 0;
static uint128_t        CACHE_ALIGNED slots[MAX_SLOTS];

// Lower 6 bits (0-63) determine cache line
// Bits 6-7 (0-3) determine quarter within cache line
#define GET_SLOT(n) ({ \
    uint32_t _n = (n); \
    (uint128_t*)((char*)slots + ((_n & 0x3FFF) << 6) + (((_n >> 14) & 0x3) << 4)); \
})

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ __volatile__("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

static int is_performance_core(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x1A) return -1;
    cpuid(0x1A, &eax, &ebx, &ecx, &edx);
    uint8_t type = (eax >> 24) & 0xFF;
    return (type == 0x40) ? 1 : (type == 0x20) ? 0 : -1;
}

int thread_pool_num_cores(void) {
    return sysconf(_SC_NPROCESSORS_CONF);
}

static void detect_topology(void) {
    int num_cores = thread_pool_num_cores();
    core_ids = malloc(num_cores * sizeof(int));
    worker_tids = calloc(num_cores, sizeof(pthread_t));

    for (int i = 0; i < num_cores; i++) core_ids[i] = -1;

    cpu_set_t orig, single;
    sched_getaffinity(0, sizeof(orig), &orig);

    int p_cores[num_cores], e_cores[num_cores], u_cores[num_cores];
    int p_count = 0, e_count = 0, u_count = 0;

    for (int cpu = 0; cpu < num_cores; cpu++) {
        if (!CPU_ISSET(cpu, &orig)) continue;

        CPU_ZERO(&single);
        CPU_SET(cpu, &single);
        sched_setaffinity(0, sizeof(single), &single);

        int type = is_performance_core();
        if (type == 1)       p_cores[p_count++] = cpu;
        else if (type == 0)  e_cores[e_count++] = cpu;
        else                 u_cores[u_count++] = cpu;
    }

    sched_setaffinity(0, sizeof(orig), &orig);

    int idx = 0;
    for (int i = 0; i < p_count; i++) core_ids[idx++] = p_cores[i];
    for (int i = 0; i < u_count; i++) core_ids[idx++] = u_cores[i];
    for (int i = 0; i < e_count; i++) core_ids[idx++] = e_cores[i];
}

static int find_free_core(void) {
    int num_cores = thread_pool_num_cores();
    uint64_t used_mask = 0;

    for (int i = 0; i < num_cores; i++) {
        pthread_t tid = (pthread_t)__atomic_load_n(
            (uint64_t*)&worker_tids[i], __ATOMIC_ACQUIRE);
        if (!tid) continue;

        cpu_set_t cpuset;
        if (pthread_getaffinity_np(tid, sizeof(cpuset), &cpuset) == 0) {
            for (int c = 0; c < num_cores; c++)
                if (CPU_ISSET(c, &cpuset)) used_mask |= (1ULL << c);
        }
    }

    for (int i = 0; i < num_cores && core_ids[i] >= 0; i++) {
        int core = core_ids[i];
        if (!(used_mask & (1ULL << core))) return core;
    }

    return -1;
}

static void* worker_thread(void* arg) {
    unsigned long my_slot = atomic_inc(&assigned);
    uint128_t* slot_ptr = GET_SLOT(my_slot);

    while (1) {
        uint128_t task = atomic128_load(slot_ptr);
        if (task && atomic128_cas(slot_ptr, &task, EMPTY)) {
            int64_t func_ptr = ((int64_t)(task & PTR_MASK)) << PTR_ALIGN_SHIFT;
            int64_t destroy_ptr = task & DATA_FLAG ? second_ptr(task) : third_ptr(task);
            void* data = (void*)(~task & DATA_FLAG ? second_ptr(task) : third_ptr(task));

            if (func_ptr) ((TaskFunc)func_ptr)(data);
            if (destroy_ptr) ((TaskDestroy)destroy_ptr)(data);

            if (task >= EXIT) return NULL;

            my_slot = atomic_inc(&assigned);
            slot_ptr = GET_SLOT(my_slot);
        } else if (!task && atomic128_cas(slot_ptr, &task, SLEEPING)) {
            syscall(SYS_futex, slot_ptr, FUTEX_WAIT_PRIVATE, SLEEPING, NULL, NULL, 0);
        }
    }
}

static pthread_t spawn_worker(int core) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }

    pthread_t tid;
    if (pthread_create(&tid, &attr, worker_thread, NULL)) {
        pthread_attr_destroy(&attr);
        return 0;
    }

    pthread_attr_destroy(&attr);

    for (int i = 0; i < thread_pool_num_cores(); i++) {
        uint64_t exp = 0;
        if (__atomic_compare_exchange_n((uint64_t*)&worker_tids[i], &exp, (uint64_t)tid, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
            return tid;
    }
    return tid;
}

static int pin_cores = 0;

void thread_pool_init(uint16_t n, int pin) {
    pin_cores = pin;
    if (pin)
        detect_topology();
    else
        worker_tids = calloc(thread_pool_num_cores(), sizeof(pthread_t));

    for (int i = 0; i < n; i++)
        spawn_worker(pin ? find_free_core() : -1);
}

int thread_pool_new_thread(void) {
    return spawn_worker(pin_cores ? find_free_core() : -1) ? 0 : -1;
}

void thread_pool_schedule_task(TaskFunc func, void* data, TaskDestroy destroy) {
    int64_t data_diff = data ? (int64_t)((char*)data - (char*)func) : 0;
    int64_t destroy_diff = destroy ? (int64_t)((char*)destroy - (char*)func) : 0;
    int64_t is_data_diff = llabs(data_diff) < llabs(destroy_diff);

    uint128_t packed = ((uint128_t)(int64_t)func) >> PTR_ALIGN_SHIFT;
    packed |= (uint128_t)is_data_diff << PTR_BITS;
    packed |= ((uint128_t)(int64_t)(is_data_diff ? destroy : data)
                >> SCND_PTR_ALIGN_SHIFT) << SECOND_PTR;
    packed |= (uint128_t)(is_data_diff ? data_diff : destroy_diff)
                >> PTR_ALIGN_SHIFT << DIFF_START;
    packed &= ~EXIT;

    uint128_t* slot_ptr = GET_SLOT(atomic_inc(&scheduled));
    uint128_t expected = atomic128_load(slot_ptr);

    while (1) {
        expected = expected == SLEEPING;
        if (atomic128_cas(slot_ptr, &expected, packed)) {
            if (expected) syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            return;
        }
        sched_yield();
    }
}

void thread_pool_join_all(void) {
    int num_cores = thread_pool_num_cores();
    pthread_t active[num_cores];
    int count = 0;

    for (int i = 0; i < num_cores; i++) {
        pthread_t tid = (pthread_t)__atomic_exchange_n(
            (uint64_t*)&worker_tids[i], 0, __ATOMIC_ACQ_REL);
        if (tid) active[count++] = tid;
    }

    for (int i = 0; i < count; i++) {
        uint128_t* slot_ptr = GET_SLOT(atomic_inc(&scheduled));
        uint128_t expected = atomic128_load(slot_ptr);

        while (1) {
            expected = expected == SLEEPING;
            if (atomic128_cas(slot_ptr, &expected, EXIT)) {
                if (expected)
                    syscall(SYS_futex, slot_ptr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
                break;
            }
            sched_yield();
        }
    }

    for (int i = 0; i < count; i++)
        pthread_join(active[i], NULL);

    atomic_store(&scheduled, 0);
    atomic_store(&assigned, 0);
}
