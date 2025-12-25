// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common/microbenchmark_utils.hpp"

#include "tests/test_utils/device_test_utils.hpp"

namespace tt::umd::test::utils {

std::pair<double, double> perf_read_write(
    const size_t buf_size,
    const uint32_t num_iterations,
    Cluster* cluster,
    const ChipId chip,
    const CoreCoord core,
    const uint32_t address) {
    std::vector<uint8_t> pattern(buf_size);
    test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < num_iterations; i++) {
        cluster->write_to_device(pattern.data(), pattern.size(), chip, core, address);
    }
    auto end = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
    double wr_bw = calc_speed(num_iterations * pattern.size(), ns);

    std::vector<uint8_t> readback(buf_size, 0x0);
    now = std::chrono::steady_clock::now();
    for (int i = 0; i < num_iterations; i++) {
        // cluster->read_from_device(readback.data(), chip, core, address, readback.size());
    }
    end = std::chrono::steady_clock::now();
    ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
    double rd_bw = calc_speed(num_iterations * readback.size(), ns);

    return std::make_pair(wr_bw, rd_bw);
}

void print_markdown_table_format(
    const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows) {
    for (const auto& header : headers) {
        std::cout << "| " << header << " ";
    }
    std::cout << "|\n";

    // Print separator row.
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << "|---";
    }
    std::cout << "|\n";

    for (const auto& row : rows) {
        for (const auto& cell : row) {
            std::cout << "| " << cell << " ";
        }
        std::cout << "|\n";
    }
}

double calc_speed(size_t bytes, uint64_t ns) { return (static_cast<double>(bytes) / (1024.0 * 1024.0)) / (ns / 1e9); }

std::string convert_double_to_string(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}
}  // namespace tt::umd::test::utils
