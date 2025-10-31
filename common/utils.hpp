/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

#include "fmt/ranges.h"

namespace tt::umd::utils {

static std::string get_abs_path(std::string path) {
    // Note that __FILE__ might be resolved at compile time to an absolute or relative address, depending on the
    // compiler.
    std::filesystem::path current_file_path = std::filesystem::path(__FILE__);
    std::filesystem::path umd_root;
    if (current_file_path.is_absolute()) {
        umd_root = current_file_path.parent_path().parent_path().parent_path();
    } else {
        std::filesystem::path umd_root_relative =
            std::filesystem::relative(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path(), "../");
        umd_root = std::filesystem::canonical(umd_root_relative);
    }
    std::filesystem::path abs_path = umd_root / path;
    return abs_path.string();
}

static std::optional<std::string> get_env_var_value(const char* env_var_name) {
    const char* env_var = std::getenv(env_var_name);
    if (!env_var) {
        return std::nullopt;
    }
    return std::string(env_var);
}

static std::optional<std::unordered_set<int>> get_unordered_set_from_string(const std::string& input) {
    std::unordered_set<int> result_set;
    std::stringstream ss(input);
    std::string token;

    while (std::getline(ss, token, ',')) {
        try {
            result_set.insert(std::stoi(token));
        } catch (const std::exception& e) {
            throw std::runtime_error(
                fmt::format("Input string is not a valid set of integers: '{}'. Error: {}", input, e.what()));
        }
    }

    if (result_set.empty()) {
        return std::nullopt;
    }

    return result_set;
}

// This ENV variable is used to specify visible devices for BOTH PCIe and JTAG interfaces depending on which one is
// active.
inline constexpr std::string_view TT_VISIBLE_DEVICES_ENV = "TT_VISIBLE_DEVICES";

static std::unordered_set<int> get_visible_devices(const std::unordered_set<int>& target_devices) {
    const std::optional<std::string> env_var_value = tt::umd::utils::get_env_var_value(TT_VISIBLE_DEVICES_ENV.data());
    return target_devices.empty() && env_var_value.has_value()
               ? tt::umd::utils::get_unordered_set_from_string(env_var_value.value())
                     .value_or(std::unordered_set<int>{})
               : target_devices;
}

template <typename... Args>
inline std::string convert_to_space_separated_string(Args&&... args) {
    return fmt::format("{}", fmt::join({fmt::to_string(std::forward<Args>(args))...}, " "));
}

template <typename T>
std::string to_hex_string(T value) {
    static_assert(std::is_integral<T>::value, "Template argument must be an integral type.");
    return fmt::format("{:#x}", value);
}

static void check_timeout(
    const std::chrono::steady_clock::time_point start_time,
    const std::chrono::milliseconds timeout,
    const std::string& error_msg) {
    if (timeout.count() == 0) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    if (elapsed > timeout) {
        throw std::runtime_error(error_msg);
    }
}

inline std::string generate_path(int card_number) {
    return fmt::format("/proc/driver/tenstorrent/{}/pids", card_number);
}

inline std::unordered_set<int> collect_pids(int pci_target_device) {
    std::ifstream infile(generate_path(pci_target_device));

    if (!infile.is_open()) {
        fmt::print(
            "Error: Could not open file {}. Make sure the card number is correct and the driver is loaded.\n",
            generate_path(pci_target_device));
        return {};
    }

    std::string line;
    std::unordered_set<int> pids;

    // Core logic for reading and converting the PIDs remains the same
    while (std::getline(infile, line)) {
        try {
            int pid = std::stoi(line);
            pids.insert(pid);
        } catch (const std::invalid_argument& e) {
            fmt::print("Warning: Skipped non-numeric line: {}\n", line);
        } catch (const std::out_of_range& e) {
            fmt::print("Warning: Skipped out-of-range number: {}\n", line);
        }
    }

    infile.close();

    // Output the results
    if (pids.empty()) {
        fmt::print("No PIDs collected from {}.\n", generate_path(pci_target_device));
    } else {
        fmt::print("Collected PIDs from {}:\n", generate_path(pci_target_device));
        for (int pid : pids) {
            fmt::print("{}\n", pid);
        }
    }

    return pids;
}

}  // namespace tt::umd::utils
