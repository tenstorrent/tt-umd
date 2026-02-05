// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nanobench.h>

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace ankerl::nanobench;

namespace tt::umd::test::utils {

inline const char* OUTPUT_ENV_VAR = "UMD_MICROBENCHMARK_RESULTS_PATH";

inline constexpr size_t ONE_KIB = 1 << 10;
inline constexpr size_t ONE_MIB = 1 << 20;
inline constexpr size_t ONE_GIB = 1 << 30;

inline void export_results(const std::string& title, std::vector<Result> const& results) {
    const char* results_path = std::getenv(OUTPUT_ENV_VAR);
    if (results_path == nullptr) {
        std::cout << OUTPUT_ENV_VAR << " not set. Results will not exported." << std::endl;
        return;
    }
    std::filesystem::path const filepath = std::filesystem::path(results_path) / (title + ".json");
    std::ofstream file(filepath);
    ankerl::nanobench::render(ankerl::nanobench::templates::json(), results, file);
    std::filesystem::path const html_filepath = std::filesystem::path(results_path) / (title + ".html");
    std::ofstream html_file(html_filepath);
    ankerl::nanobench::render(ankerl::nanobench::templates::htmlBoxplot(), results, html_file);
}

inline void export_results(const Bench& bench) { export_results(bench.title(), bench.results()); }

}  // namespace tt::umd::test::utils
