sample_deps = [dpx_trans_dep, args_dep]

samples = [
    ['zpp_bits_sample', [], []],
    ['tcp_server', [], []],
    ['tcp_client', [], []],
    # ['rdma_server', [], []],
    # ['rdma_client', [], []],
    # ['doca_comch_ctrl_path_client', [], []],
    # ['doca_comch_ctrl_path_server', [], []],
    # ['doca_comch_data_path_client', [], []],
    # ['doca_comch_data_path_server', [], []],
]

foreach sample : samples

    executable(
        sample[0],
        files([sample[0] + '.cxx'] + sample[1]),
        dependencies: sample_deps + sample[2],
    )

endforeach
