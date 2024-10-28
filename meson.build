project(
    'DataPathXpressTransport',
    'c',
    'cpp',
    version: '0.0.1',
    default_options: [
        'warning_level=3',
        'optimization=3',
        'cpp_std=c++20',
        'c_std=c2x',
        'buildtype=release',
    ],
)

c_cpp_extra_args = [
    '-Wno-c99-extensions',
    '-Wno-gnu-anonymous-struct',
    '-Wno-nested-anon-types',
    '-Wno-zero-length-array',
    '-Wno-deprecated-enum-enum-conversion',
]

cpp_extra_args = [
    '-Wno-c++23-extensions',
]

if get_option('buildtype') == 'release'
    c_cpp_extra_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO'
else
    c_cpp_extra_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE'
    # c_cpp_extra_args += '-DENABLE_CHECK_FOOTPRINT'
endif

add_project_arguments(cpp_extra_args, language: ['cpp'])
add_project_arguments(c_cpp_extra_args, language: ['c', 'cpp'])

cmake = import('cmake')

glaze_opt = cmake.subproject_options()
glaze_opt.add_cmake_defines(
    {
        'glaze_ENABLE_AVX2': 'ON',
        'glaze_DEVELOPER_MODE': 'OFF',
    },
)

basic_deps = [
    dependency('threads', required: true),
    dependency('boost', modules: ['fiber', 'context'], required: true),
    subproject('dbg-macro').get_variable('dbg_macro_dep'),
    subproject('zpp_bits').get_variable('zpp_bits_dep'),
    subproject(
        'spdlog',
        default_options: ['tests=disabled', 'std_format=enabled'],
        required: true,
    ).get_variable('spdlog_dep'),
    cmake.subproject('glaze', options: glaze_opt).dependency('glaze_glaze'),
]

rdma_deps = [
    dependency('librdmacm', required: true),
    dependency('libibverbs', required: true),
]

uring_dep = subproject('liburing').get_variable('uring')

args_dep = subproject('args').get_variable('args_dep')

doca_deps = [
    dependency('libmlx5', required: true),
    dependency('doca-common', required: true),
    dependency('doca-rdma', required: true),
    dependency('doca-dma', required: true),
    dependency('doca-comch', required: true),
]

basic_incs = [
    include_directories('lib'),
]

subdir('lib')
subdir('sample')
subdir('tool')
