[CCode (cheader_filename = "thread-manager.h")]
public delegate void SimpleCallback();

[CCode (cheader_filename = "thread-manager.h", has_target = false)]
public delegate void TaskCleanup(int counter);

[CCode (cheader_filename = "thread-manager.h")]
extern ThreadPool* thread_pool_create(int max_queue_size);

[CCode (cheader_filename = "thread-manager.h")]
extern void thread_pool_destroy(ThreadPool* pool);

[CCode (cheader_filename = "thread-manager.h")]
extern uint thread_pool_num_cores(ThreadPool* pool);

[CCode (cheader_filename = "thread-manager.h", delegate_target = false)]
extern void thread_pool_add_task(ThreadPool* pool, SimpleCallback func, TaskCleanup cleanup, int counter);

[CCode (cheader_filename = "thread-manager.h")]
extern bool thread_pool_cancel_task(ThreadPool* pool, int counter);

[CCode (cheader_filename = "thread-manager.h")]
extern void thread_pool_mark_ready(ThreadPool* pool, int counter);

[Compact]
public class ThreadManager {
    [Compact]
    [CCode (ref_function="simple_callback_data_ref", unref_function="simple_callback_data_unref")]
    private class SimpleCallbackData {
        public int ref_count = 0;
        public SimpleCallback func;

        public SimpleCallbackData(owned SimpleCallback func) {
            this.func = (owned)func;
            AtomicInt.inc(ref ref_count);
        }

        [CCode (cname="simple_callback_data_ref")]
        public unowned SimpleCallbackData ref() {
            AtomicInt.inc(ref ref_count);
            return this;
        }

        [CCode (cname="simple_callback_data_unref")]
        public void unref() {
            if (AtomicInt.dec_and_test(ref ref_count)) {
                this.func = null;
            }
        }

    }

    private const int MAX_QUEUE_SIZE = 256;
    private static ThreadPool* pool = null;
    private static SimpleCallbackData[] callbacks;
    private static Mutex mu;
    private static Cond jobs_done;
    private static int _next_free_slot;
    private static int _atomic_job_counter;

    public static int worker_threads {
        get {
            return (int)thread_pool_num_cores(pool);
        }
    }

    private ThreadManager() { }

    public static void initialize() {
        if (pool != null) {
            error("ThreadManager already initialized");
        }
        mu = Mutex();
        pool = thread_pool_create(MAX_QUEUE_SIZE);
        callbacks = new SimpleCallbackData[MAX_QUEUE_SIZE];
        jobs_done = Cond();
    }

    private static int find_next_free_slot(int start_slot) {
        for (int i = start_slot + 1; i < MAX_QUEUE_SIZE; i++) {
            if (callbacks[i] == null) {
                return i;
            }
        }
        return MAX_QUEUE_SIZE;
    }

    private static void cleanup_callback(int event_id) {
        mu.lock();
        callbacks[event_id] = null;

        // Only update _next_free_slot if this slot is lower than current next_free
        if (event_id < AtomicInt.get(ref _next_free_slot)) {
            AtomicInt.set(ref _next_free_slot, event_id);
        }

        AtomicInt.dec_and_test(ref _atomic_job_counter);
        jobs_done.broadcast();
        mu.unlock();
    }

    public static void shutdown() {
        mu.lock();
        if (pool == null) {
            mu.unlock();
            error("ThreadManager not initialized or already shut down");
        }

        while (AtomicInt.get(ref _atomic_job_counter) > 0) {
            jobs_done.wait(mu);
        }
        thread_pool_destroy(pool);
        pool = null;
        mu.unlock();
    }

    public static void run(owned SimpleCallback callback, Cancellable cancellable) {
        mu.lock();

        while (AtomicInt.get(ref _atomic_job_counter) >= MAX_QUEUE_SIZE) {
            jobs_done.wait(mu);
        }

        if (cancellable.is_cancelled()) {
            mu.unlock();
            return;
        }

        int event_id = AtomicInt.get(ref _next_free_slot);
        callbacks[event_id] = new SimpleCallbackData((owned)callback);

        thread_pool_add_task(pool, callbacks[event_id].func, cleanup_callback, event_id);
        cancellable.connect(() => thread_pool_cancel_task(pool, event_id));

        AtomicInt.inc(ref _atomic_job_counter);
        AtomicInt.set(ref _next_free_slot, find_next_free_slot(event_id));
        thread_pool_mark_ready(pool, event_id);

        mu.unlock();
    }
}
