/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>

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

static void check_timeout(
    const std::chrono::steady_clock::time_point start_ms, const uint64_t timeout_ms, const std::string& error_msg) {
    if (timeout_ms == 0) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch() - start_ms.time_since_epoch())
            .count();
    if (elapsed_ms > timeout_ms) {
        throw std::runtime_error(error_msg);
    }
}
}  // namespace tt::umd::utils
