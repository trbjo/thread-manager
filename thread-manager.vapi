namespace ThreadManager {
    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_run", has_type_id=false)]
    public static extern void run(owned TaskFunc task);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_run_new_thread", has_type_id=false)]
    public static extern int run_new_thread(owned TaskFunc task);

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_initialize", has_type_id=false)]
    public static extern void initialize();

    [CCode (cheader_filename = "thread-manager.h", cname = "thread_pool_destroy", has_type_id=false)]
    public static extern void destroy();

    [CCode (cheader_filename = "thread-manager.h", has_target = true, delegate_target = true, has_type_id=false)]
    public delegate void TaskFunc();
}
