// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "utils.hpp"

using namespace tt::umd::microbenchmarks;

TEST(MicrobenchmarkHost, Memcpy) {
    Bench bench;
    bench.title("Host memcpy").unit("byte").warmup(2).epochIterations(10).minEpochIterations(10);

    for (const size_t size : {ONE_KIB, ONE_MIB, 16 * ONE_MIB}) {
        std::vector<uint8_t> src(size, 0xAB);
        std::vector<uint8_t> dst(size);
        bench.batch(size).run("memcpy " + std::to_string(size) + " B", [&]() {
            std::memcpy(dst.data(), src.data(), size);
            doNotOptimizeAway(dst.data());
        });
    }

    export_results(bench);
}
