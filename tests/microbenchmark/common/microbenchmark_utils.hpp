// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "umd/device/cluster.hpp"

namespace tt::umd::test::utils {

/**
 * Return performance of read and write operations to specific chip and core in MBs/s.
 *
 * @param buf_size Size of the buffer in bytes.
 * @param num_iterations Number of iterations to perform for read and write operations.
 * @param cluster The cluster to perform the operations on.
 * @param chip The logical chip ID to perform the operations on.
 * @param core The core coordinates to perform the operations on.
 * @return A pair containing the write bandwidth and read bandwidth in MB/s.
 */
std::pair<double, double> perf_read_write(
    const size_t buf_size,
    const uint32_t num_iterations,
    Cluster* cluster,
    const ChipId chip,
    const CoreCoord core,
    const uint32_t address = 0);

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
