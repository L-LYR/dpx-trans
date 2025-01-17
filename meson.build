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

# Compiler Arguments

c_cpp_extra_args = [
    '-Wno-c99-extensions',
    '-Wno-gnu-anonymous-struct',
    '-Wno-nested-anon-types',
    '-Wno-zero-length-array',
    '-Wno-deprecated-enum-enum-conversion',
    '-Wno-flexible-array-extensions',
]

cpp_extra_args = [
    '-Wno-c++23-extensions',
]

if get_option('buildtype') == 'release'
    c_cpp_extra_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO'
    c_cpp_extra_args += '-DNDEBUG'
elif get_option('buildtype') == 'plain'
    c_cpp_extra_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG'
    c_cpp_extra_args += '-DNDEBUG'
elif get_option('buildtype') == 'debugoptimized'
    c_cpp_extra_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG'
elif get_option('buildtype') == 'debug'
    c_cpp_extra_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE'
    c_cpp_extra_args += '-DENABLE_FOOTPRINT'
else
    error('Unknown build type')
endif

add_project_arguments(cpp_extra_args, language: ['cpp'])
add_project_arguments(c_cpp_extra_args, language: ['c', 'cpp'])

# Dependencies

## pthread
pthread_dep = dependency('threads', required: true)
## glaze
cmake = import('cmake')
glaze_opt = cmake.subproject_options()
glaze_opt.add_cmake_defines(
    {
        'glaze_ENABLE_AVX2': 'ON',
        'glaze_DEVELOPER_MODE': 'OFF',
    },
)
glaze_dep = cmake.subproject('glaze', options: glaze_opt).dependency(
    'glaze_glaze',
)
## Boost.Fiber
boost_fiber_dep = dependency(
    'boost',
    modules: ['fiber', 'context'],
    required: true,
)
## args
args_dep = subproject('args').get_variable('args_dep')
## dbg-macro
dbg_dep = subproject('dbg-macro').get_variable('dbg_macro_dep')
## zpp_bits
zpp_bits_dep = subproject('zpp_bits').get_variable('zpp_bits_dep')
## spdlog
spdlog_dep = subproject(
    'spdlog',
    default_options: ['tests=disabled', 'std_format=enabled'],
    required: true,
).get_variable('spdlog_dep')
## librdmacm & libibverbs
rdma_deps = [
    dependency('librdmacm', required: true),
    dependency('libibverbs', required: true),
]
## liburing
uring_dep = dependency('liburing')
## DOCA
doca_deps = [
    dependency('libmlx5', required: true),
    dependency('doca-common', required: true),
    dependency('doca-rdma', required: true),
    dependency('doca-comch', required: true),
    dependency('doca-dma', required: true),
]
## SPSCQueue
SPSCQueue_dep = subproject('SPSCQueue').get_variable('SPSCQueue_dep')
## MPMCQueue
MPMCQueue_dep = subproject('MPMCQueue').get_variable('MPMCQueue_dep')

# Common Include Directories

base_incs = [
    include_directories('include'),
    include_directories('src'),
]

subdir('include')
subdir('src')
subdir('example')
