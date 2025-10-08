/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.hpp>

#include "tt-logger/tt-logger.hpp"

namespace tt::umd {
class DebugMode {
public:
    static inline void enable() { enabled = true; }

    static inline bool debug_mode() { return enabled; }

private:
    static bool enabled;
};

template <typename... Ts>
bool tt_assert_debug(
    char const* file,
    int line,
    const std::string& assert_type,
    bool condition,
    char const* condition_str,
    Ts const&... messages) {
    if (not condition) {
        if (DebugMode::debug_mode()) {
            log_warning(LogUMD, messages...);
            return false;
        } else {
            ::tt::assert::tt_throw(file, line, assert_type, condition_str, messages...);
        }
    }
    return true;
}
}  // namespace tt::umd

#define TT_ASSERT_DEBUG(condition, ...) \
    ::tt::umd::tt_assert_debug(__FILE__, __LINE__, "TT_ASSERT_DEBUG", (condition), #condition, ##__VA_ARGS__)
