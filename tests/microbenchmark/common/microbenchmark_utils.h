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

/**
 * Prints a table in Markdown format. Headers are printed as the first row, followed by a separator row,
 * and then the data rows. Headers length must match the length of each row. Example:
 * | Size (MB) | Host -> Device Tensix L1 (MB/s) | Device Tensix L1 -> Host (MB/s) |
 * |---|---|---|
 * | 1.00 | 13157.70 | 2493.65 |
 *
 * @param headers The headers of the table.
 * @param rows The rows of the table, where each row is a vector of strings.
 */
void print_markdown_table_format(
    const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows);

/**
 * Calculates the speed in MB/s given the number of bytes and the time in nanoseconds.
 *
 * @param bytes The number of bytes processed.
 * @param ns The time taken in nanoseconds.
 * @return The speed in MB/s.
 */
double calc_speed(size_t bytes, uint64_t ns);

/**
 * Converts a double value to a string with fixed-point notation and two decimal places.
 *
 * @param value The double value to convert.
 * @return A string representation of the double value.
 */
std::string convert_double_to_string(double value);

}  // namespace tt::umd::test::utils
