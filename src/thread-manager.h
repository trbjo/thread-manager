#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

typedef void (*RawThreadFunc)(void* data);
typedef void (*TaskCleanup)(int counter);

typedef struct ThreadPool ThreadPool;

ThreadPool* thread_pool_create(int max_queue_size);
void thread_pool_destroy(ThreadPool* pool);

int thread_pool_check_space(ThreadPool* pool);
unsigned int thread_pool_num_cores(ThreadPool* pool);
void thread_pool_add_task(ThreadPool* pool, RawThreadFunc task, void* callback_data, TaskCleanup cleanup, int counter);
void thread_pool_mark_ready(ThreadPool* pool, int counter);
int thread_pool_cancel_task(ThreadPool* pool, int counter);

#endif
