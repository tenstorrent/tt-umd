/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <spdlog/spdlog.h>
#include <stdlib.h>

#include <iostream>
#include <sstream>

#include "backtrace.hpp"

namespace tt {
template <typename A, typename B>
struct OStreamJoin {
    OStreamJoin(A const& a, B const& b, char const* delim = " ") : a(a), b(b), delim(delim) {}

    A const& a;
    B const& b;
    char const* delim;
};

template <typename A, typename B>
std::ostream& operator<<(std::ostream& os, tt::OStreamJoin<A, B> const& join) {
    os << join.a << join.delim << join.b;
    return os;
}
}  // namespace tt

template <typename A, typename B>
struct fmt::formatter<tt::OStreamJoin<A, B>> : fmt::ostream_formatter {};

namespace tt::assert {

inline void tt_assert_message(std::ostream& os) {}

template <typename T, typename... Ts>
void tt_assert_message(std::ostream& os, T const& t, Ts const&... ts) {
    if constexpr (sizeof...(ts) == 0) {
        os << t << std::endl;
        return;
    }

    std::ostringstream oss;
    oss << t;
    std::string format_str = oss.str();

    size_t placeholder_count = 0;
    size_t pos = 0;
    while ((pos = format_str.find("{}", pos)) != std::string::npos) {
        placeholder_count++;
        pos += 2;
    }

    if (placeholder_count == 0) {
        os << t << std::endl;
        ((os << ts << std::endl), ...);
        return;
    }

    if (placeholder_count != sizeof...(ts)) {
        throw std::runtime_error(
            "Failed formatting: placeholder count mismatch: format string '" + format_str + "' has " +
            std::to_string(placeholder_count) + " placeholders but " + std::to_string(sizeof...(ts)) +
            " arguments provided");
    }

    // constexpr has to be present since build fails compiler detects
    // that certain objects with unformattable types will be used here.
    if constexpr ((fmt::is_formattable<Ts>::value && ...)) {
        std::string formatted = fmt::format(fmt::runtime(format_str), ts...);
        os << formatted << std::endl;
    } else {
        throw std::runtime_error("Failed to format string: " + format_str + ", arguments not formattable by fmt.");
    }
}

template <typename... Ts>
[[noreturn]] void tt_throw(
    char const* file, int line, const std::string& assert_type, char const* condition_str, Ts const&... messages) {
    std::stringstream trace_message_ss = {};
    trace_message_ss << assert_type << " @ " << file << ":" << line << ": " << condition_str << std::endl;
    if constexpr (sizeof...(messages) > 0) {
        trace_message_ss << fmt::format(messages...) << std::endl;
    }
    trace_message_ss << "Backtrace:\n";
    trace_message_ss << tt::assert::backtrace_to_string(100, 3, " --- ");
    trace_message_ss << std::flush;
    spdlog::default_logger()->flush();
    throw std::runtime_error(trace_message_ss.str());
}

template <typename... Ts>
void tt_assert(
    char const* file,
    int line,
    const std::string& assert_type,
    bool condition,
    char const* condition_str,
    Ts const&... messages) {
    if (not condition) {
        ::tt::assert::tt_throw(file, line, assert_type, condition_str, messages...);
    }
}

}  // namespace tt::assert

#define TT_ASSERT(condition, ...) \
    ::tt::assert::tt_assert(__FILE__, __LINE__, "TT_ASSERT", (condition), #condition, ##__VA_ARGS__)
#define TT_THROW(...) ::tt::assert::tt_throw(__FILE__, __LINE__, "TT_THROW", "tt::exception", ##__VA_ARGS__)
