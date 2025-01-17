dpx_trans_deps = []
dpx_trans_deps += zpp_bits_dep
dpx_trans_deps += pthread_dep
dpx_trans_deps += spdlog_dep
dpx_trans_deps += glaze_dep
dpx_trans_deps += boost_fiber_dep

dpx_trans_incs = []
dpx_trans_incs += base_incs

dpx_trans_src = [
    'util/hex_dump.cxx',
    'util/spin_lock.cxx',
    'util/thread_util.cxx',
]

dpx_trans = library(
    'dpx_trans',
    files(dpx_trans_src),
    dependencies: dpx_trans_deps,
    include_directories: dpx_trans_incs,
    install: true,
)

dpx_trans_dep = declare_dependency(
    link_with: dpx_trans,
    dependencies: dpx_trans_deps,
    include_directories: dpx_trans_incs,
)
