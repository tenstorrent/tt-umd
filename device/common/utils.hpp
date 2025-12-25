// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <unistd.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

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

// This ENV variable is used to specify visible devices for BOTH PCIe and JTAG interfaces depending on which one is
// active.
inline constexpr std::string_view TT_VISIBLE_DEVICES_ENV = "TT_VISIBLE_DEVICES";

static std::unordered_set<int> get_visible_devices(const std::unordered_set<int>& target_devices) {
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
                throw std::runtime_error("Failed to create synchronization pipe");
            }
        }
    }

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
