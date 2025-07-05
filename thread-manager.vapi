namespace Threads {
    [CCode (cheader_filename = "thread-manager.h", has_target = true, delegate_target = true, has_type_id=false)]
    public delegate void TaskFunc();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_run", has_type_id=false)]
    public static extern void run(owned TaskFunc task);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_spawn_joinable", has_type_id=false)]
    public static extern uint spawn_joinable(owned TaskFunc task);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_join", has_type_id=false)]
    public static extern void join(uint thread_id);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_new_thread", has_type_id=false)]
    public static extern void new_thread();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_num_cores", has_type_id=false)]
    public static extern int num_cores();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_join_all", has_type_id=false)]
    public static extern void join_all();

    [CCode (cheader_filename = "immintrin.h", cname = "_mm_pause", has_type_id=false)]
    public static extern void pause();
}

namespace Atomics {
    [CCode (cheader_filename = "stdatomic.h", cname = "atomic_load", has_type_id=false)]
    public static extern int load(ref int ptr);

    [CCode (cheader_filename = "stdatomic.h", cname = "atomic_exchange", has_type_id=false)]
    public static extern int exchange(ref int ptr, int value);

    [CCode (cheader_filename = "stdatomic.h", cname = "atomic_compare_exchange_strong", has_type_id=false)]
    public static extern bool cas(ref int ptr, ref int expected, int value);

    [CCode (cheader_filename = "stdatomic.h", cname = "atomic_exchange", has_type_id=false)]
    public static extern uint uexchange(ref uint ptr, uint value);

    [CCode (cheader_filename = "thread-manager.h", cname = "atomic_inc", has_type_id=false)]
    public static extern int inc(ref int ptr);

    [CCode (cheader_filename = "thread-manager.h", cname = "atomic_dec", has_type_id=false)]
    public static extern int dec(ref int ptr);

    [CCode (cheader_filename = "thread-manager.h", cname = "atomic_store", has_type_id=false)]
    public static extern void store(ref int ptr, int value);
}

namespace Spinlock {
    [SimpleType]
    [IntegerType (rank = 5)]
    [CCode (has_type_id = false, cname = "atomic_int")]
    public struct AtomicInt { }

    [CCode (cheader_filename = "atomic_helpers.h", cname = "is_locked", has_type_id=false)]
    public static extern bool is_locked (ref AtomicInt lock);

    [CCode (cheader_filename = "atomic_helpers.h", cname = "spin_lock", has_type_id=false)]
    public static extern void spin_lock (ref AtomicInt lock);

    [CCode (cheader_filename = "atomic_helpers.h", cname = "spin_unlock", has_type_id=false)]
    public static extern void spin_unlock (ref AtomicInt lock);
}

