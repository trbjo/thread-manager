namespace Threads {
    [CCode (cheader_filename = "thread-manager.h", has_target = true, delegate_target = true, has_type_id=false)]
    public delegate void TaskFunc();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_init", has_type_id=false)]
    public static extern void init(int cores, bool pin_cores);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_pin_caller", has_type_id=false)]
    public static extern void pin_caller();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_run", has_type_id=false)]
    public static extern void run(owned TaskFunc task);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_join_all", has_type_id=false)]
    public static extern void join_all();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_num_cores", has_type_id=false)]
    public static extern int num_cores();
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

[CCode (cheader_filename = "stdatomic.h")]
namespace AtomicUlong {
    [CCode (cname = "atomic_store")]
    public static void store (ref ulong ptr, ulong val);

    [CCode (cname = "atomic_load")]
    public static ulong load (ref ulong ptr);
}

namespace Spinlock {
    [CCode (cname = "aligned_atomic_int", has_type_id = false)]
    public struct AtomicInt { }

    [CCode (cname = "aligned_atomic_int_new", cheader_filename = "atomic_helpers.h")]
    public static extern AtomicInt* new ();

    [CCode (cname = "aligned_atomic_int_free", cheader_filename = "atomic_helpers.h")]
    public static extern void free (AtomicInt* ptr);

    [CCode (cname = "is_locked", cheader_filename = "atomic_helpers.h")]
    public static extern bool is_locked (AtomicInt* lock);

    [CCode (cname = "spin_lock", cheader_filename = "atomic_helpers.h")]
    public static extern void spin_lock (AtomicInt* lock);

    [CCode (cname = "spin_unlock", cheader_filename = "atomic_helpers.h")]
    public static extern void spin_unlock (AtomicInt* lock);
}
