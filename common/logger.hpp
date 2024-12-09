/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <type_traits>
#if defined(UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT) && (UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT == 1)
#include <pybind11/iostream.h>
#endif

#include "backtrace.hpp"
#include "fmt/color.h"
#include "fmt/core.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"
#include "fmt/std.h"

namespace tt {

#define LOGGER_TYPES_DEVICE \
    X(Always)               \
    X(SiliconDriver)        \
    X(EmulationDriver)

enum LogTypeDevice : uint32_t {

// clang-format off
#define X(a) Log ## a,
    LOGGER_TYPES_DEVICE
#undef X
    LogTypeDevice_Count,
    // clang-format on
};
static_assert(LogTypeDevice_Count < 64, "Exceeded number of log types");

#pragma GCC visibility push(hidden)

class LoggerDevice {
public:
    static constexpr char const* type_names[LogTypeDevice_Count] = {
    // clang-format off
#define X(a) #a,
      LOGGER_TYPES_DEVICE
#undef X
        // clang-format on
    };

    enum class Level {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Profile = 3,
        Warning = 4,
        Error = 5,
        Fatal = 6,
        Disabled = 7,

        Count,
    };

    static constexpr char const* level_names[] = {
        "TRACE",
        "DEBUG",
        "INFO",
        "PROFILE",
        "WARNING",
        "ERROR",
        "FATAL",
        "DISABLED",
    };
    Level min_level = Level::Info;
    std::mutex fd_mutex;
    static_assert(
        (sizeof(level_names) / sizeof(level_names[0])) == static_cast<std::underlying_type_t<Level>>(Level::Count));

    static constexpr fmt::color level_color[] = {
        fmt::color::gray,
        fmt::color::gray,
        fmt::color::cornflower_blue,
        fmt::color::orange_red,
        fmt::color::orange,
        fmt::color::red,
        fmt::color::red,
        fmt::color::red,
    };

    static_assert(
        (sizeof(level_color) / sizeof(level_color[0])) == static_cast<std::underlying_type_t<Level>>(Level::Count));

    // TODO: we should sink this into some common cpp file, marking inline so maybe it's per lib instead of per TU
    static inline LoggerDevice& get() {
        static LoggerDevice logger;
        return logger;
    }

    template <typename... Args>
    inline void log_level_type(Level level, LogTypeDevice type, char const* fmt, Args&&... args) {
        if ((1 << type) & mask) {
#if defined(UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT) && (UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT == 1)
            pybind11::scoped_ostream_redirect stream(*fd);
#endif
            std::string timestamp_str = get_current_time();
            fmt::terminal_color timestamp_color = fmt::terminal_color::green;
            timestamp_str = fmt::format(fmt::fg(timestamp_color), "{}", timestamp_str);

            std::string level_str = fmt::format(
                fmt::fg(level_color[static_cast<std::underlying_type_t<Level>>(level)]) | fmt::emphasis::bold,
                "{:8}",
                level_names[static_cast<std::underlying_type_t<Level>>(level)]);

            fmt::terminal_color type_color = fmt::terminal_color::cyan;
            std::string type_str = fmt::format(fmt::fg(type_color), "{:15}", type_names[type]);

            std::string str;
            fmt::format_to(std::back_inserter(str), "{} | {} | {} - ", timestamp_str, level_str, type_str);
            fmt::format_to(std::back_inserter(str), fmt::runtime(fmt), std::forward<Args>(args)...);

            std::lock_guard<std::mutex> lock(fd_mutex);
            *fd << str << std::endl;
        }
    }

    void flush() { *fd << std::flush; }

private:
    LoggerDevice() {
        static char const* env = std::getenv("LOGGER_TYPES");
        if (env) {
            if (strstr(env, "All")) {
                mask = 0xFFFFFFFFFFFFFFFF;
            } else {
                std::uint32_t mask_index = 0;
                for (char const* type_name : type_names) {
                    mask |= (strstr(env, type_name) != nullptr) << mask_index;
                    mask_index++;
                }
            }
        } else {
            mask = 0xFFFFFFFFFFFFFFFF;
        }

        static char const* level_env = std::getenv("LOGGER_LEVEL");
        if (level_env) {
            std::string level_str = level_env;
            std::transform(
                level_str.begin(), level_str.end(), level_str.begin(), [](unsigned char c) { return std::toupper(c); });
            std::underlying_type_t<Level> level_index = 0;
            for (char const* level_name : level_names) {
                if (level_str == level_name) {
                    min_level = static_cast<Level>(level_index);
                }
                level_index++;
            }
        }

#if !defined(UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT) || (UTILS_LOGGER_PYTHON_OSTREAM_REDIRECT == 0)
        static char const* file_env = std::getenv("LOGGER_FILE");
        if (file_env) {
            log_file.open(file_env);
            if (log_file.is_open()) {
                fd = &log_file;
            }
        }
#endif
    }

    // Returns the current timestamp in 'YYYY-MM-DD HH:MM:SS.MMM' format, e.g. 2023-06-26 11:39:38.432
    static std::string get_current_time() {
        auto now = std::chrono::system_clock::now();
        auto current_time = std::chrono::system_clock::to_time_t(now);

        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_ms.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&current_time), "%F %T");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::ofstream log_file;
    std::ostream* fd = &std::cout;
    std::uint64_t mask = (1 << LogAlways);
};

#pragma GCC visibility pop

#undef LOGGER_TYPES_DEVICE

template <typename... Args>
static void log_trace_(LogTypeDevice type, std::string const& src_info, char const* fmt, Args&&... args) {
    LoggerDevice::get().log_level_type(LoggerDevice::Level::Trace, type, fmt, src_info, std::forward<Args>(args)...);
}
}  // namespace tt

#define log_custom(level, type, str, ...)                                                                      \
    {                                                                                                          \
        if (static_cast<std::underlying_type_t<tt::LoggerDevice::Level>>(level) >=                             \
            static_cast<std::underlying_type_t<tt::LoggerDevice::Level>>(tt::LoggerDevice::get().min_level)) { \
            tt::LoggerDevice::get().log_level_type(level, type, str, ##__VA_ARGS__);                           \
        }                                                                                                      \
    }

#define log_info(type, str, ...) \
    { log_custom(tt::LoggerDevice::Level::Info, type, str, ##__VA_ARGS__); }

#define log_warning(type, str, ...) \
    { log_custom(tt::LoggerDevice::Level::Warning, type, str, ##__VA_ARGS__); }

#define log_error(str, ...) \
    { log_custom(tt::LoggerDevice::Level::Error, tt::LogAlways, str, ##__VA_ARGS__); }

#define log_fatal(str, ...)                                                                                         \
    {                                                                                                               \
        log_custom(tt::LoggerDevice::Level::Fatal, tt::LogAlways, str, ##__VA_ARGS__);                              \
        tt::LoggerDevice::get().flush();                                                                            \
        throw std::runtime_error(                                                                                   \
            fmt::format(str, ##__VA_ARGS__) + "\nbacktrace:\n" + tt::assert::backtrace_to_string(100, 1, " --- ")); \
    }

#define log_assert(cond, str, ...)                                                         \
    {                                                                                      \
        if (!(cond)) {                                                                     \
            log_custom(tt::LoggerDevice::Level::Fatal, tt::LogAlways, str, ##__VA_ARGS__); \
            tt::LoggerDevice::get().flush();                                               \
            throw std::runtime_error(                                                      \
                fmt::format(str, ##__VA_ARGS__) + "\nbacktrace:\n" +                       \
                tt::assert::backtrace_to_string(100, 1, " --- "));                         \
        }                                                                                  \
    }

#ifdef TT_DEBUG_LOGGING

#define log_debug(type, str, ...) \
    { log_custom(tt::LoggerDevice::Level::Debug, type, str, ##__VA_ARGS__); }

#define log_trace(type, ...)                                                                                      \
    {                                                                                                             \
        if (static_cast<std::underlying_type_t<tt::LoggerDevice::Level>>(tt::LoggerDevice::Level::Trace) >=       \
            static_cast<std::underlying_type_t<tt::LoggerDevice::Level>>(LoggerDevice::get().min_level)) {        \
            tt::log_trace_(                                                                                       \
                type, fmt::format(fmt::fg(fmt::color::green), "{}:{}", __FILE__, __LINE__), "{} - " __VA_ARGS__); \
        }                                                                                                         \
    }

#define log_profile(str, ...) \
    { log_custom(tt::LoggerDevice::Level::Profile, tt::LogProfile, str, ##__VA_ARGS__); }

#else

#define log_trace(...) ((void)0)
#define log_debug(...) ((void)0)
#define log_profile(...) ((void)0)
#endif
