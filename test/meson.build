catch2 = subproject(
    'catch2',
    default_options: ['tests=false', 'install=false'],
    required: true,
)
catch2_dep = catch2.get_variable('catch2_dep')
catch2_with_main_dep = catch2.get_variable('catch2_with_main_dep')

executable(
    'util_test',
    files('util_test.cxx'),
    dependencies: [dpx_trans_dep, catch2_with_main_dep],
)
