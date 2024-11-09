samples = [
    ['zpp_bits_sample', [], [zpp_bits_dep, dbg_dep]],

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
