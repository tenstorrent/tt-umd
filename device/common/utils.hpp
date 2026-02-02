// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>

#include "fmt/ranges.h"

namespace tt::umd::utils {

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

static std::vector<std::string> split_tt_visible_devices_string(const std::string& tt_visible_devices_string) {
    std::vector<std::string> device_tokens;
    std::stringstream ss(tt_visible_devices_string);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(token.find_last_not_of(" \n\r\t") + 1);
        token.erase(0, token.find_first_not_of(" \n\r\t"));
        if (!token.empty()) {
            device_tokens.push_back(token);
        }
    }

    return device_tokens;
}

static std::optional<std::unordered_set<std::string>> get_unordered_set_from_bdf_string(const std::string& input) {
    std::unordered_set<std::string> result_set;
    std::stringstream ss(input);
    std::string token;

    while (std::getline(ss, token, ',')) {
        // Trim whitespace from the token.
        token.erase(token.find_last_not_of(" \n\r\t") + 1);
        token.erase(0, token.find_first_not_of(" \n\r\t"));

        if (token.empty()) {
            continue;
        }

        // Basic BDF format validation: should be like "0000:02:00.0".
        if (token.length() < 8 || token.find(':') == std::string::npos || token.find('.') == std::string::npos) {
            throw std::runtime_error(fmt::format(
                "Invalid BDF format in input string: '{}'. Expected format: 'domain:bus:device.function' (e.g., "
                "'0000:02:00.0')",
                token));
        }

        result_set.insert(token);
    }

    if (result_set.empty()) {
        return std::nullopt;
    }

    return result_set;
}

// This ENV variable is used to specify visible devices for BOTH PCIe and JTAG interfaces depending on which one is
// active.
// This ENV variable is used to specify visible devices by PCI BDF (Bus:Device.Function) addresses.
// Format: comma-separated BDF addresses like "0000:02:00.0,0000:03:00.0"
// When set, TT_VISIBLE_DEVICES takes precedence over TT_VISIBLE_DEVICES for PCIe devices.
inline constexpr std::string_view TT_VISIBLE_DEVICES_ENV = "TT_VISIBLE_DEVICES";

enum class TT_VISIBLE_DEVICES_Format {
    Integer,  // Format contains comma-separated integers (e.g., "0,1,2")
    BDF,      // Format contains comma-separated PCI BDF addresses (e.g., "0000:01:00.0,0000:02:00.0")
    NotSet,   // Environment variable is not set
    Empty,    // Environment variable is set but empty
    Invalid   // Format is neither valid integers nor valid BDF addresses
};

/**
 * Check the format of TT_VISIBLE_DEVICES environment variable.
 * @return TT_VISIBLE_DEVICES_Format indicating the detected format.
 */
static inline TT_VISIBLE_DEVICES_Format check_tt_visible_devices_format() {
    const std::optional<std::string> env_var_value = get_env_var_value(TT_VISIBLE_DEVICES_ENV.data());

    if (!env_var_value.has_value()) {
        return TT_VISIBLE_DEVICES_Format::NotSet;
    }

    if (env_var_value.value().empty()) {
        return TT_VISIBLE_DEVICES_Format::Empty;
    }

    const std::string& input = env_var_value.value();
    std::stringstream ss(input);
    std::string token;
    bool has_tokens = false;
    bool could_be_integer = true;
    bool could_be_bdf = true;

    while (std::getline(ss, token, ',')) {
        // Trim whitespace from the token.
        token.erase(token.find_last_not_of(" \n\r\t") + 1);
        token.erase(0, token.find_first_not_of(" \n\r\t"));

        if (token.empty()) {
            continue;
        }

        has_tokens = true;

        // Check if token could be an integer.
        if (could_be_integer) {
            try {
                std::stoi(token);
            } catch (const std::exception&) {
                could_be_integer = false;
            }
        }

        // Check if token could be a BDF address.
        if (could_be_bdf) {
            // Basic BDF format validation: should be like "0000:02:00.0".
            if (token.length() < 8 || token.find(':') == std::string::npos || token.find('.') == std::string::npos) {
                could_be_bdf = false;
            }
        }

        // If neither format is possible, we can return early.
        if (!could_be_integer && !could_be_bdf) {
            return TT_VISIBLE_DEVICES_Format::Invalid;
        }
    }

    if (!has_tokens) {
        return TT_VISIBLE_DEVICES_Format::Empty;
    }

    // If both formats are still possible, prefer integer format for backwards compatibility
    // In practice, this should be rare since BDF and integer formats are quite distinct.
    if (could_be_integer && could_be_bdf) {
        return TT_VISIBLE_DEVICES_Format::Integer;
    }

    if (could_be_integer) {
        return TT_VISIBLE_DEVICES_Format::Integer;
    }

    if (could_be_bdf) {
        return TT_VISIBLE_DEVICES_Format::BDF;
    }

    return TT_VISIBLE_DEVICES_Format::Invalid;
}

static inline std::unordered_set<int> get_visible_devices(const std::unordered_set<int>& target_devices) {
    const std::optional<std::string> env_var_value = get_env_var_value(TT_VISIBLE_DEVICES_ENV.data());
    return target_devices.empty() && env_var_value.has_value()
               ? get_unordered_set_from_string(env_var_value.value()).value_or(std::unordered_set<int>{})
               : target_devices;
}

static inline std::unordered_set<std::string> get_visible_bdfs() {
    const std::optional<std::string> env_var_value = get_env_var_value(TT_VISIBLE_DEVICES_ENV.data());
    return env_var_value.has_value()
               ? get_unordered_set_from_bdf_string(env_var_value.value()).value_or(std::unordered_set<std::string>{})
               : std::unordered_set<std::string>{};
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

enum class TimeoutAction { Throw, Return };

/**
 * Throw std::runtime_error or return true if `timeout` amount of time has elapsed since `start_time`.
 * @param start_time Point in time when the measured event started.
 * @param timeout Time expected for event to complete.
 * @param error_msg Error message to log or pass to std::runtime_error.
 * @param action Decide which action (throw or return false) is done when timeout elapses.
 */
static inline bool check_timeout(
    const std::chrono::steady_clock::time_point start_time,
    const std::chrono::milliseconds timeout,
    const std::string& error_msg,
    TimeoutAction action = TimeoutAction::Throw) {
    // A timeout of 0 can never time out.
    if (timeout.count() == 0) {
        return false;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    if (elapsed > timeout) {
        if (action == TimeoutAction::Throw) {
            throw std::runtime_error(error_msg);
        }
        log_warning(LogUMD, error_msg);
        return true;
    }
    return false;
}

}  // namespace tt::umd::utils

constexpr bool is_arm_platform() {
#if defined(__aarch64__) || defined(__arm__)
    return true;
#else
    return false;
#endif
}

constexpr bool is_riscv_platform() {
#if defined(__riscv)
    return true;
#else
    return false;
#endif
}
