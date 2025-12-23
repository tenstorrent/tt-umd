// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nanobench.h>

#include <filesystem>
#include <fstream>

using namespace ankerl::nanobench;

namespace tt::umd::test::utils {

inline const char* OUTPUT_ENV_VAR = "UMD_MICROBENCHMARK_RESULTS_PATH";

inline constexpr size_t ONE_KB = 1 << 10;
inline constexpr size_t ONE_MB = 1 << 20;
inline constexpr size_t ONE_GB = 1 << 30;

inline void export_results(const Bench& bench) {
    if (const char* results_path = std::getenv(OUTPUT_ENV_VAR)) {
        std::filesystem::path filepath = std::filesystem::path(results_path) / (bench.title() + ".json");
        std::ofstream file(filepath);
        ankerl::nanobench::render(ankerl::nanobench::templates::json(), bench, file);
    }
}

}  // namespace tt::umd::test::utils
