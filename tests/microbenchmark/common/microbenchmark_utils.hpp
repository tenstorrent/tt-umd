// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nanobench.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace ankerl::nanobench;

namespace tt::umd::test::utils {

inline const char* OUTPUT_ENV_VAR = "UMD_MICROBENCHMARK_RESULTS_PATH";

inline constexpr size_t ONE_KIB = 1 << 10;
inline constexpr size_t ONE_MIB = 1 << 20;
inline constexpr size_t ONE_GIB = 1 << 30;

// Single timestamp shared across all benches in one process invocation, so all
// JSON/HTML artifacts from a given run land in the same subdirectory.
inline const std::string& get_run_timestamp() {
    static const std::string ts = [] {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H-%M-%S");
        return oss.str();
    }();
    return ts;
}

inline void export_results(const std::string& title, std::vector<Result> const& results) {
    const char* results_path = std::getenv(OUTPUT_ENV_VAR);
    if (results_path == nullptr) {
        std::cout << OUTPUT_ENV_VAR << " not set. Results will not exported." << std::endl;
        return;
    }
    std::filesystem::path results_dir = std::filesystem::path(results_path) / get_run_timestamp();
    std::filesystem::create_directories(results_dir);
    std::filesystem::path filepath = results_dir / (title + ".json");
    std::ofstream file(filepath);
    ankerl::nanobench::render(ankerl::nanobench::templates::json(), results, file);
    std::filesystem::path html_filepath = results_dir / (title + ".html");
    std::ofstream html_file(html_filepath);
    ankerl::nanobench::render(ankerl::nanobench::templates::htmlBoxplot(), results, html_file);
}

inline void export_results(const Bench& bench) { export_results(bench.title(), bench.results()); }

}  // namespace tt::umd::test::utils
