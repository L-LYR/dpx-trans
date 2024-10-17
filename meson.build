project(
    'DataPathXpressTransport',
    'c',
    'cpp',
    'java',
    version: '0.0.1',
    default_options: [
        'warning_level=3',
        'optimization=3',
        'cpp_std=c++26',
        'c_std=c2x',
        'buildtype=release',
    ],
)

add_project_arguments(
    '-Wno-c++23-extensions',
    language: ['cpp'],
)
add_project_arguments(
    '-Wno-c99-extensions',
    '-Wno-gnu-anonymous-struct',
    '-Wno-nested-anon-types',
    '-Wno-zero-length-array',
    language: ['c', 'cpp'],
)

dependencies = []
dependencies += dependency('threads', required: true)

include_directories = []

dependencies += subproject('dbg-macro').get_variable('dbg_macro_dep')
dependencies += subproject('zpp_bits').get_variable('zpp_bits_dep')
dependencies += subproject(
    'spdlog',
    default_options: [
        'tests=disabled',
        'std_format=enabled',
    ],
).get_variable('spdlog_dep')
dependencies += subproject('liburing').get_variable('uring')

cmake = import('cmake')

glaze_opt = cmake.subproject_options()
glaze_opt.add_cmake_defines(
    {
        'glaze_ENABLE_AVX2': 'ON',
        'glaze_DEVELOPER_MODE': 'OFF',
    },
)
dependencies += cmake.subproject(
    'glaze',
    options: glaze_opt,
).dependency('glaze_glaze')

args_dep = subproject('args').get_variable('args_dep')

subdir('lib')
subdir('sample')
