// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tt::umd::test::utils {
static inline void print_markdown_table_format(
    const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows) {
    // Print header row
    for (const auto& header : headers) {
        std::cout << "| " << header << " ";
    }
    std::cout << "|\n";

    // Print separator row
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << "|---";
    }
    std::cout << "|\n";

    // Print data rows
    for (const auto& row : rows) {
        for (const auto& cell : row) {
            std::cout << "| " << cell << " ";
        }
        std::cout << "|\n";
    }
}

static inline double calc_speed(size_t bytes, uint64_t ns) {
    return (static_cast<double>(bytes) / (1024.0 * 1024.0)) / (ns / 1e9);
}

static inline std::string convert_double_to_string(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}
}  // namespace tt::umd::test::utils
