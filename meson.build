project('thread-manager', 'c')
project_name = 'io.github.trbjo.vala-libraries.thread-manager'

inc = include_directories('src')

optimization_flags = [
    '-O3',                    # Base optimization level
    '-march=native',          # CPU-specific optimizations
    '-fno-plt',               # For function pointer calls
    '-fno-stack-protector',   # Remove stack protection overhead
    '-funroll-loops',         # Unroll loops
]

vapi_dir = meson.current_source_dir()

thread_manager_lib = static_library('thread-manager',
    sources: [ 'src/thread-manager.c' ],
    include_directories: inc,
    install: true,
    c_args: ['-D_GNU_SOURCE'] + optimization_flags,
)

thread_manager_dep = declare_dependency(
    link_with: thread_manager_lib,
    include_directories: [inc, include_directories('.')],
    variables: {'vapi_dir': vapi_dir}
)
