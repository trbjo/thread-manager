#define _GNU_SOURCE
#include "thread-manager.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sched.h>
#include <unistd.h>
#include <cpuid.h>

typedef struct {
    int total_cores;
    int performance_cores;
    int efficiency_cores;
} cpu_topology_t;

typedef enum {
    TASK_IDLE,        // Initial state when task is queued
    TASK_PENDING      // Ready to be executed
} TaskState;

typedef struct {
    RawThreadFunc func;          // 8 bytes
    void* callback_data;         // 8 bytes
    TaskCleanup cleanup;         // 8 bytes
    atomic_int state;            // 4 bytes
    int counter;                 // 4 bytes
    char _padding[8];            // 8 bytes padding to ensure 8-byte alignment
} __attribute__((aligned(8))) Task;

struct ThreadPool {
    pthread_t* threads;           // All threads
    pthread_t* active_threads;    // Array of size max_threads to track busy threads
    atomic_int max_threads;
    atomic_int shutdown;          // Flag to signal shutdown (0 = running, 1 = shutdown)

    Task* task_queue;            // 24-31
    atomic_int queue_head;       // 32-35
    atomic_int queue_tail;       // 36-39
    atomic_int queue_size;       // 40-43
    int max_queue_size;          // 44-47 (uses existing padding space)

    pthread_mutex_t mutex;
    pthread_cond_t task_available;

    cpu_topology_t topology;
    cpu_set_t performance_cores_mask;
};

static int is_performance_core(int core_id) {
    cpu_set_t original_cpuset;
    cpu_set_t test_cpuset;

    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_cpuset);

    CPU_ZERO(&test_cpuset);
    CPU_SET(core_id, &test_cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &test_cpuset);

    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    int is_perf = 0;  // Default to NOT being a performance core

    // Check for hybrid architecture support
    __cpuid_count(0x7, 0, eax, ebx, ecx, edx);
    if (!(edx & (1 << 15))) {  // If no hybrid support
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_cpuset);
        return 0;  // Return false for non-hybrid CPUs
    }

    // Get hybrid info
    __cpuid_count(0x1A, 0, eax, ebx, ecx, edx);
    if (!(eax & 0x3)) {  // If hybrid info not valid
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_cpuset);
        return 0;  // Return false if no valid hybrid info
    }

    // Get core type
    __cpuid_count(0x1A, 1, eax, ebx, ecx, edx);
    int core_type = (eax >> 24) & 0xFF;
    is_perf = (core_type == 0x40);  // 0x40 = Performance core (Alder Lake P-core)

    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_cpuset);

    return is_perf;
}

static cpu_topology_t detect_cpu_topology(cpu_set_t* performance_mask) {
    cpu_topology_t topology = {0};
    CPU_ZERO(performance_mask);

    topology.total_cores = sysconf(_SC_NPROCESSORS_CONF);

    // Detect P-cores and their HT pairs
    for (int i = 0; i < topology.total_cores; i++) {
        if (is_performance_core(i)) {
            topology.performance_cores++;
            CPU_SET(i, performance_mask);
        }
    }

    topology.efficiency_cores = topology.total_cores - topology.performance_cores;

    return topology;
}

static Task* get_task_from_queue(ThreadPool* pool, int index) {
    if (!pool || !pool->task_queue || index < 0 || index >= pool->max_queue_size) {
        return NULL;
    }
    return &pool->task_queue[index % pool->max_queue_size];
}

static int find_free_thread_slot(ThreadPool* pool) {
    for (int i = 0; i < pool->max_threads; i++) {
        if (pool->active_threads[i] == 0) {
            return i;
        }
    }
    return -1;
}

static void* worker_thread(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    pthread_t tid = pthread_self();

    // Set thread affinity to performance cores
    pthread_setaffinity_np(tid, sizeof(cpu_set_t), &pool->performance_cores_mask);

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        int slot = find_free_thread_slot(pool);
        while (!atomic_load(&pool->shutdown) && (pool->queue_size == 0 || slot == -1)) {
            pthread_cond_wait(&pool->task_available, &pool->mutex);
            slot = find_free_thread_slot(pool);
        }

        if (atomic_load(&pool->shutdown)) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        Task* task_ptr = get_task_from_queue(pool, pool->queue_head);
        if (!task_ptr || atomic_load(&task_ptr->state) != TASK_PENDING) {
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        int task_counter = task_ptr->counter;
        RawThreadFunc task_func = task_ptr->func;
        void* task_data = task_ptr->callback_data;
        TaskCleanup task_cleanup = task_ptr->cleanup;

        pool->queue_head = (pool->queue_head + 1) % pool->max_queue_size;
        pool->queue_size--;
        pool->active_threads[slot] = tid;

        pthread_mutex_unlock(&pool->mutex);

        task_func(task_data);
        if (task_cleanup) {
            task_cleanup(task_counter);
        }

        pthread_mutex_lock(&pool->mutex);
        pool->active_threads[slot] = 0;
        pthread_cond_broadcast(&pool->task_available);
        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}

unsigned int thread_pool_num_cores(ThreadPool* pool) {
    return pool->topology.performance_cores;
}

ThreadPool* thread_pool_create(int max_queue_size) {
    ThreadPool* pool = malloc(sizeof(ThreadPool));
    if (!pool) return NULL;

    pool->max_queue_size = max_queue_size;
    pool->topology = detect_cpu_topology(&pool->performance_cores_mask);
    unsigned int max_threads = pool->topology.performance_cores;

    pool->threads = malloc(sizeof(pthread_t) * max_threads);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    pool->active_threads = calloc(max_threads, sizeof(pthread_t));
    if (!pool->active_threads) {
        free(pool->threads);
        free(pool);
        return NULL;
    }

    pool->task_queue = malloc(sizeof(Task) * pool->max_queue_size);
    if (!pool->task_queue) {
        free(pool->active_threads);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    atomic_init(&pool->max_threads, max_threads);
    atomic_init(&pool->queue_head, 0);
    atomic_init(&pool->queue_tail, 0);
    atomic_init(&pool->queue_size, 0);
    atomic_init(&pool->shutdown, 0);

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->task_available, NULL);

    for (int i = 0; i < max_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    }

    return pool;
}

void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    atomic_store(&pool->shutdown, 1);
    pthread_cond_broadcast(&pool->task_available);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->max_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->task_available);

    free(pool->active_threads);
    free(pool->threads);
    free(pool->task_queue);
    free(pool);
}

void thread_pool_add_task(ThreadPool* pool, RawThreadFunc task, void* callback_data, TaskCleanup cleanup, int counter) {
    pthread_mutex_lock(&pool->mutex);

    Task new_task = {0};
    new_task.func = task;
    new_task.callback_data = callback_data;
    new_task.cleanup = cleanup;
    new_task.counter = counter;
    atomic_init(&new_task.state, TASK_IDLE);

    pool->task_queue[pool->queue_tail] = new_task;
    pool->queue_tail = (pool->queue_tail + 1) % pool->max_queue_size;
    pool->queue_size++;

    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_mark_ready(ThreadPool* pool, int counter) {
    pthread_mutex_lock(&pool->mutex);

    int current = pool->queue_head;
    int remaining = pool->queue_size;

    while (remaining > 0) {
        Task* task = &pool->task_queue[current];
        if (task->counter == counter && atomic_load(&task->state) == TASK_IDLE) {
            atomic_store(&task->state, TASK_PENDING);
            pthread_cond_signal(&pool->task_available);
            break;
        }
        current = (current + 1) % pool->max_queue_size;
        remaining--;
    }

    pthread_mutex_unlock(&pool->mutex);
}

int thread_pool_cancel_task(ThreadPool* pool, int counter) {
    if (!pool) return 0;

    pthread_mutex_lock(&pool->mutex);

    int current = pool->queue_head;
    int remaining = pool->queue_size;
    int found = 0;

    while (remaining > 0) {
        Task* task = &pool->task_queue[current];
        if (task->counter == counter &&
            (atomic_load(&task->state) == TASK_IDLE ||
             atomic_load(&task->state) == TASK_PENDING)) {

            if (task->cleanup) {
                task->cleanup(task->counter);
            }

            int shift_index = current;
            int next_index = (shift_index + 1) % pool->max_queue_size;
            int tasks_to_shift = remaining - 1;

            while (tasks_to_shift > 0) {
                memcpy(&pool->task_queue[shift_index],
                       &pool->task_queue[next_index],
                       sizeof(Task));

                shift_index = next_index;
                next_index = (next_index + 1) % pool->max_queue_size;
                tasks_to_shift--;
            }

            pool->queue_size--;
            pool->queue_tail = (pool->queue_tail - 1 + pool->max_queue_size) % pool->max_queue_size;

            found = 1;
            break;
        }
        current = (current + 1) % pool->max_queue_size;
        remaining--;
    }

    pthread_mutex_unlock(&pool->mutex);
    return found;
}
