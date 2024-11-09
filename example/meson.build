example_deps = [dpx_trans_dep, args_dep]


examples = [
    ['tcp_server', [], example_deps],
    ['tcp_client', [], example_deps],
    ['rdma_server', [], example_deps],
    ['rdma_client', [], example_deps],
    ['doca_comch_client', [], example_deps],
    ['doca_comch_server', [], example_deps],
]

foreach example : examples

    executable(
        example[0],
        files([example[0] + '.cxx'] + example[1]),
        dependencies: example[2],
    )

endforeach
