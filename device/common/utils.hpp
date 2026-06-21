// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/ranges.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "umd/device/utils/error.hpp"

namespace tt::umd::utils {

inline std::optional<std::string> get_env_var_value(const char* env_var_name) {
    const char* env_var = std::getenv(env_var_name);
    if (!env_var) {
        return std::nullopt;
    }
    return std::string(env_var);
}

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

// This ENV variable is used to specify visible devices for BOTH PCIe and JTAG interfaces depending on which one is
// active.
// This ENV variable is used to specify visible devices by PCI BDF (Bus:Device.Function) addresses.
// Format: comma-separated BDF addresses like "0000:02:00.0,0000:03:00.0"
// When set, TT_VISIBLE_DEVICES takes precedence over TT_VISIBLE_DEVICES for PCIe devices.
inline constexpr std::string_view TT_VISIBLE_DEVICES_ENV = "TT_VISIBLE_DEVICES";

inline std::unordered_set<int> get_visible_devices(const std::unordered_set<int>& target_devices) {
    const std::optional<std::string> env_var_value = get_env_var_value(TT_VISIBLE_DEVICES_ENV.data());
    return target_devices.empty() && env_var_value.has_value()
               ? get_unordered_set_from_string(env_var_value.value()).value_or(std::unordered_set<int>{})
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

/**
 * Checks if `timeout` amount of time has elapsed since `start_time`.
 * @param start_time Point in time when the measured event started.
 * @param timeout Time expected for event to complete.
 * @returns True if `timeout` amount of time has elapsed since `start_time`.
 */
inline bool check_timeout(
    const std::chrono::steady_clock::time_point start_time, const std::chrono::milliseconds timeout) noexcept {
    // A timeout of 0 can never time out.
    if (timeout.count() == 0) {
        return false;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    return elapsed > timeout;
}

/**
 * Adaptive polling loop. Busy-polls `predicate` for `busy_poll_window`, then sleeps
 * `poll_interval` between checks until the predicate returns true or `timeout` elapses.
 *
 * @param predicate Callable returning bool; polled until it returns true.
 * @param timeout Maximum total time to wait.
 * @param busy_poll_window How long to spin before backing off to sleep-based polling.
 * @param poll_interval Sleep duration between checks once past the busy window.
 * @returns True if predicate became true before timeout, false otherwise.
 *          Caller is responsible for handling the timeout case.
 */
template <typename Predicate>
inline bool poll_until(
    Predicate predicate,
    const std::chrono::milliseconds timeout,
    const std::chrono::microseconds busy_poll_window,
    const std::chrono::microseconds poll_interval) {
    // Ensure the predicate is callable.
    static_assert(std::is_invocable_v<Predicate>, "poll_until: The predicate provided must be callable.");

    // Enforce strict bool return type.
    using ReturnType = std::invoke_result_t<Predicate>;
    static_assert(std::is_same_v<ReturnType, bool>, "poll_until: Predicate must return 'bool'.");

    const auto start = std::chrono::steady_clock::now();
    while (!predicate()) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > timeout) {
            return false;
        }
        if (elapsed > busy_poll_window) {
            std::this_thread::sleep_for(poll_interval);
        }
    }
    return true;
}

enum class TimeoutAction { Throw, Return };

/**
 * Throw std::runtime_error or return true if `timeout` amount of time has elapsed since `start_time`.
 * @param start_time Point in time when the measured event started.
 * @param timeout Time expected for event to complete.
 * @param error_msg Error message to log or pass to RuntimeError.
 * @param action Decide which action (throw or return false) is done when timeout elapses.
 */
inline bool check_timeout(
    const std::chrono::steady_clock::time_point start_time,
    const std::chrono::milliseconds timeout,
    const std::string& error_msg,
    TimeoutAction action = TimeoutAction::Throw) {
    bool timed_out = check_timeout(start_time, timeout);
    if (timed_out) {
        auto error = UMD_THROW_OR_RETURN(action == TimeoutAction::Throw, error::RuntimeError, error_msg);
        log_warning(LogUMD, error.message());
    }
    return timed_out;
}

class MultiProcessPipe {
private:
    static constexpr int PIPE_READ = 0;
    static constexpr int PIPE_WRITE = 1;

    // Stores [read_fd, write_fd] for each child.
    std::vector<std::array<int, 2>> child_pipes;
    int num_children;

public:
    explicit MultiProcessPipe(int count) : num_children(count) {
        child_pipes.resize(num_children);
        for (int i = 0; i < num_children; ++i) {
            if (pipe(child_pipes[i].data()) == -1) {
                int saved_errno = errno;
                for (int j = 0; j < i; ++j) {
                    close(child_pipes[j][PIPE_READ]);
                    close(child_pipes[j][PIPE_WRITE]);
                }
                errno = saved_errno;
                UMD_THROW(
                    error::RuntimeError,
                    "Failed to create synchronization pipe. errno: {}" + std::string(std::strerror(saved_errno)));
            }
        }
    }

    MultiProcessPipe(const MultiProcessPipe&) = delete;
    MultiProcessPipe& operator=(const MultiProcessPipe&) = delete;

    ~MultiProcessPipe() {
        for (auto& p : child_pipes) {
            if (p[PIPE_READ] != -1) {
                close(p[PIPE_READ]);
            }
            if (p[PIPE_WRITE] != -1) {
                close(p[PIPE_WRITE]);
            }
        }
    }

    // Called by the Child process after it is fully initialized.
    void signal_ready_from_child(int child_index) {
        // Close the read end we don't need in the child.
        close(child_pipes[child_index][PIPE_READ]);
        child_pipes[child_index][PIPE_READ] = -1;

        char sync_token = '1';
        if (write(child_pipes[child_index][PIPE_WRITE], &sync_token, 1) == -1) {
            perror("Barrier: Failed to write sync token");
        }

        // Close the write end after signaling.
        close(child_pipes[child_index][PIPE_WRITE]);
        child_pipes[child_index][PIPE_WRITE] = -1;
    }

    // Called by the Parent process to block until all children signal.
    bool wait_for_all_children(int timeout_seconds_per_process = 5) {
        for (int i = 0; i < num_children; ++i) {
            // Close the write end we don't need in the parent.
            if (child_pipes[i][PIPE_WRITE] != -1) {
                close(child_pipes[i][PIPE_WRITE]);
                child_pipes[i][PIPE_WRITE] = -1;
            }

            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(child_pipes[i][PIPE_READ], &read_set);

            struct timeval timeout;
            timeout.tv_sec = timeout_seconds_per_process;
            timeout.tv_usec = 0;

            // Wait here for up to timeout_seconds.
            int ready = select(child_pipes[i][PIPE_READ] + 1, &read_set, nullptr, nullptr, &timeout);

            if (ready <= 0) {
                return false;
            }

            char sync_token;
            if (read(child_pipes[i][PIPE_READ], &sync_token, 1) <= 0) {
                return false;
            }

            close(child_pipes[i][PIPE_READ]);
            child_pipes[i][PIPE_READ] = -1;
        }
        return true;
    }
};

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

}  // namespace tt::umd::utils

namespace tt::umd {

template <typename Alignment, typename Value>
inline void throw_if_not_aligned(Value value, const std::string& what) {
    static_assert(std::is_integral_v<Alignment>, "Alignment type must be integral.");
    static_assert(std::is_integral_v<Value>, "Value type must be integral.");
    if (value % sizeof(Alignment) != 0) {
        UMD_THROW(error::RuntimeError, what + " must be " + std::to_string(sizeof(Alignment)) + "-byte aligned.");
    }
}

inline void validate_register_access(uint64_t addr, size_t size) {
    throw_if_not_aligned<uint32_t>(addr, "Register address");
    throw_if_not_aligned<uint32_t>(size, "Register access size");
}

}  // namespace tt::umd
