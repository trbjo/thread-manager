project('thread-manager', 'c')
project_name = 'io.github.trbjo.vala-libraries.thread-manager'

inc = include_directories('src')

thread_manager_lib = static_library('thread-manager',
    sources: ['src/thread-manager.c', 'src/atomic_helpers.c'],
    include_directories: inc,
    install: false,
    c_args: ['-D_GNU_SOURCE', '-O2', '-fno-plt'],
    # c_args: ['-D_GNU_SOURCE', '-O3', '-march=native', '-fno-plt', '-fno-stack-protector'],
)

vapi_dir = meson.current_source_dir()

thread_manager_dep = declare_dependency(
    link_with: thread_manager_lib,
    include_directories: [inc, include_directories('.')],
    variables: {'vapi_dir': vapi_dir}
)
