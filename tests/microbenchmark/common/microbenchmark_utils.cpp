// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "common/microbenchmark_utils.h"

namespace tt::umd::test::utils {
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
