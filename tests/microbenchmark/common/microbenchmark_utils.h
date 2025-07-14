// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <iostream>
#include <vector>

namespace tt::umd::test::utils {
void print_markdown_table_format(
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
}  // namespace tt::umd::test::utils
