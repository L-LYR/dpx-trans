dpx_trans_deps = []

dpx_trans_deps += basic_deps
dpx_trans_deps += glaze_dep
dpx_trans_deps += spdlog_dep
dpx_trans_deps += zpp_bits_dep
dpx_trans_deps += uring_dep
dpx_trans_deps += doca_deps
dpx_trans_deps += rdma_deps

dpx_trans_incs = []
dpx_trans_incs += basic_incs

dpx_trans_src = [
    './doca/device.cxx',
    './priv/context.cxx',
    './priv/endpoint.cxx',
    './priv/tcp/connection.cxx',
    './priv/tcp/endpoint.cxx',
    './priv/verbs/connection.cxx',
    './priv/verbs/endpoint.cxx',
]

dpx_trans = library(
    'dpx_trans',
    files(dpx_trans_src),
    dependencies: dpx_trans_deps,
    include_directories: dpx_trans_incs,
)

dpx_trans_dep = declare_dependency(
    link_with: dpx_trans,
    dependencies: dpx_trans_deps,
    include_directories: dpx_trans_incs,
)
