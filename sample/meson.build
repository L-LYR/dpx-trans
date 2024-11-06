sample_deps = [dpx_trans_dep, args_dep]

samples = [
    ['zpp_bits_sample', [], sample_deps],
    ['tcp_server', [], sample_deps],
    ['tcp_client', [], sample_deps],
    ['rdma_server', [], sample_deps],
    ['rdma_client', [], sample_deps],
    ['doca_comch_client', [], sample_deps],
    ['doca_comch_server', [], sample_deps],
    ['min_comch_client', [], doca_deps],
    ['min_comch_server', [], doca_deps],
]

foreach sample : samples

    executable(
        sample[0],
        files([sample[0] + '.cxx'] + sample[1]),
        dependencies: sample[2],
    )

endforeach
