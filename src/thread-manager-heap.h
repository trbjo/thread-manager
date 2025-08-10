#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <stdatomic.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#define MAX_SLOTS (((uint16_t)~0) + 1)

#define EMPTY 0
#define SLEEPING 1
#define EXIT_FLAG 2
#define TASK_FLAG 4
#define FLAG_BITS 7
#define TASK_OR_EXIT_FLAG (TASK_FLAG | EXIT_FLAG)

#define PTR_SHIFT 3
#define IS_THREAD_ID(x) ((x) > 1 && !((x) & (TASK_FLAG | EXIT_FLAG)))

#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#define atomic_inc(ptr) __atomic_fetch_add((ptr), 1, __ATOMIC_RELAXED)
#define atomic_dec(ptr) __atomic_fetch_sub((ptr), 1, __ATOMIC_RELAXED)
#define a_load(ptr) atomic_load((atomic_ulong*)ptr)
#define a_compare_exchange_strong(ptr, expected, desired) atomic_compare_exchange_strong((atomic_ulong*)ptr, expected, desired)
#define a_compare_exchange_weak(ptr, expected, desired) atomic_compare_exchange_weak((atomic_ulong*)ptr, expected, desired)
#define a_exchange(ptr, desired) atomic_exchange((atomic_ulong*)ptr, desired)
#define a_store(ptr, desired) atomic_store((atomic_ulong*)ptr, desired)

typedef struct {
    atomic_ulong value;
    char padding[CACHE_LINE_SIZE - sizeof(atomic_ulong)];
} CACHE_ALIGNED atomic_ulong_padded;

typedef void (*TaskFunc)(void *data);
typedef void (*TaskDestroy)(void *data);

#define thread_pool_join(thread_id) pthread_join(thread_id, NULL)

uint16_t thread_pool_schedule_task(TaskFunc func, void *data, TaskDestroy destroy, uint64_t should_exit);
#define thread_pool_run(func, data, destroy) thread_pool_schedule_task(func, data, destroy, 0)

void* worker_thread(void* arg);
#define thread_pool_new_thread() pthread_create(&(pthread_t){0}, NULL, worker_thread, NULL)
uint64_t thread_pool_spawn_joinable(TaskFunc task, void *task_user_data, TaskDestroy task_destroy);
void thread_pool_join_all();

int thread_pool_num_cores();

#endif
