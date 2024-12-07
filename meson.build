project('thread-manager', ['vala', 'c'])
project_name = 'io.github.trbjo.vala-libraries.thread-manager'

proj_deps = {
    'glib': dependency('glib-2.0'),
    'gio': dependency('gio-2.0'),
}

deps_list = []
foreach dep_name, dep : proj_deps
    deps_list += dep
endforeach

inc = include_directories('src')

thread_manager_lib = static_library('thread-manager',
    sources: [
        'src/thread-manager.vala',
        'src/thread-manager.c'
    ],
    dependencies: deps_list,
    include_directories: inc,
    install: true,
    c_args: ['-D_GNU_SOURCE'],
    vala_header: meson.current_build_dir() / 'thread-manager-vala.h',
    vala_vapi: 'thread-manager.vapi'
)

thread_manager_dep = declare_dependency(
    link_with: thread_manager_lib,
    dependencies: deps_list,
    include_directories: [inc, include_directories('.')],
)
