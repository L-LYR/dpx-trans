sample_deps = basic_deps + args_dep
sample_incs = basic_incs

samples = [
    ['zpp_bits_sample', [], []],
    ['tcp_server', ['echo.cxx'], [uring_dep]],
    ['tcp_client', ['echo.cxx'], [uring_dep]],
    ['rdma_server', ['echo.cxx'], rdma_deps],
    ['rdma_client', ['echo.cxx'], rdma_deps],
]

foreach sample : samples

    executable(
        sample[0],
        files([sample[0] + '.cxx'] + sample[1]),
        dependencies: sample_deps + sample[2],
        include_directories: sample_incs,
        link_with: dpx_trans,
    )

endforeach
