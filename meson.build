project(
    'DataPathXpressTransport',
    'c',
    'cpp',
    'java',
    version: '0.0.1',
    default_options: [
        'warning_level=3',
        'optimization=3',
        'cpp_std=c++23',
        'c_std=c2x',
        'buildtype=release',
    ],
)

dependencies = []
dependencies += dependency('threads', required: true)

include_directories = []

dependencies += subproject('liburing').get_variable('uring')

subdir('lib')
subdir('sample')
