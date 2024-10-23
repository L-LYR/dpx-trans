sample_deps = basic_deps + args_dep
sample_incs = basic_incs

samples = [
    ['zpp_bits_sample', []],
    ['tcp_server', [uring_dep]],
    ['tcp_client', [uring_dep]],
    ['rdma_server', rdma_deps],
    ['rdma_client', rdma_deps],
]

foreach sample : samples

    executable(
        sample[0],
        files(sample[0] + '.cxx'),
        dependencies: sample_deps + sample[1],
        include_directories: sample_incs,
    )

endforeach
