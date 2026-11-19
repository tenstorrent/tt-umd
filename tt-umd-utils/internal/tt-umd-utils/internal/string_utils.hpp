// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tt-umd-utils/error.hpp"

namespace tt::umd::utils {

inline std::optional<std::unordered_set<int>> get_unordered_set_from_string(const std::string& input) {
    std::unordered_set<int> result_set;
    std::stringstream ss(input);
    std::string token;

    while (std::getline(ss, token, ',')) {
        try {
            result_set.insert(std::stoi(token));
        } catch (const std::exception& e) {
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Input string is not a valid set of integers: '{}'. Error: {}", input, e.what()));
        }
    }

    if (result_set.empty()) {
        return std::nullopt;
    }

    return result_set;
}

inline std::vector<std::string> split_string_by_comma(const std::string& input_string) {
    std::vector<std::string> device_tokens;
    std::stringstream ss(input_string);
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

// Check if a string is a valid integer (all digits).
inline bool is_integer_string(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

// Check if a string looks like a BDF (Bus:Device.Function) format.
// BDF format contains ':' and '.' and only valid hex/BDF characters.
inline bool is_bdf_string(const std::string& str) {
    return (str.find(':') != std::string::npos || str.find('.') != std::string::npos) &&
           (str.find_first_not_of("0123456789abcdefABCDEF.:") == std::string::npos);
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

}  // namespace tt::umd::utils
