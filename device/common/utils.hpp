// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/ranges.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
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

// Coarse, host-wide check for whether any RDMA-capable port (RoCE or InfiniBand) is currently up,
// by scanning /sys/class/infiniband/*/ports/*/state for a port whose state name is ACTIVE.
inline bool has_any_active_rdma_port() {
    static const std::filesystem::path infiniband_class_path = "/sys/class/infiniband";
    std::error_code ec;
    if (!std::filesystem::is_directory(infiniband_class_path, ec)) {
        return false;
    }

    for (const auto& device_entry : std::filesystem::directory_iterator(infiniband_class_path, ec)) {
        const std::filesystem::path ports_path = device_entry.path() / "ports";
        if (!std::filesystem::is_directory(ports_path, ec)) {
            continue;
        }

        for (const auto& port_entry : std::filesystem::directory_iterator(ports_path, ec)) {
            std::ifstream state_file(port_entry.path() / "state");
            std::string state_line;
            if (!state_file.is_open() || !std::getline(state_file, state_line)) {
                continue;
            }

            // Format is "<n>: <NAME>", e.g. "4: ACTIVE".
            const size_t colon = state_line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string state_name = state_line.substr(colon + 1);
            state_name.erase(0, state_name.find_first_not_of(" \t"));
            state_name.erase(state_name.find_last_not_of(" \t\r\n") + 1);

            if (state_name == "ACTIVE") {
                return true;
            }
        }
    }

    return false;
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

// Overrides the host id that discovery stamps on the cluster descriptor. Needed in containers and
// VMs, where gethostname() returns the container/guest name and not an identity of the group of
// accelerators. On bare metal it should be left unset, so the OS hostname is used.
inline constexpr std::string_view TT_HOST_ID_ENV = "TT_HOST_ID";

// A host id has to fit in the fixed 64-byte buffer that tt-metal packs it into, NUL included.
inline constexpr size_t HOST_ID_MAX_LENGTH = 63;

inline std::string trim_whitespace(const std::string& input) {
    const size_t first = input.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) {
        return "";
    }
    return input.substr(first, input.find_last_not_of(" \n\r\t") - first + 1);
}

// Returns why host_id is not a legal host id, or nullopt when it is legal. Legal ids match
// ^[A-Za-z0-9]([A-Za-z0-9._-]*[A-Za-z0-9])?$ and are at most HOST_ID_MAX_LENGTH long. That is
// deliberately hostname-shaped: host ids are currently hostname-valued and have to join against
// hostnames in the factory system descriptor.
inline std::optional<std::string> get_host_id_error(const std::string& host_id) {
    auto is_alphanumeric = [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0;
    };

    if (host_id.empty()) {
        return "it is empty";
    }
    if (host_id.size() > HOST_ID_MAX_LENGTH) {
        return fmt::format("it is {} characters long, the limit is {}", host_id.size(), HOST_ID_MAX_LENGTH);
    }
    if (!is_alphanumeric(host_id.front()) || !is_alphanumeric(host_id.back())) {
        return "it has to start and end with an alphanumeric character";
    }
    for (const char character : host_id) {
        if (!is_alphanumeric(character) && character != '.' && character != '-' && character != '_') {
            return fmt::format("it contains '{}', and only alphanumerics, '.', '-' and '_' are allowed", character);
        }
    }
    return std::nullopt;
}

inline void validate_host_id(const std::string& host_id, const std::string_view source) {
    const std::optional<std::string> host_id_error = get_host_id_error(host_id);
    if (host_id_error.has_value()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Invalid host id \"{}\" from {}: {}.", host_id, source, host_id_error.value()));
    }
}

// Host id of the group of accelerators this process is running on: $TT_HOST_ID when it is set to a
// non-empty value, otherwise the OS hostname. Returns nullopt when neither can be used, which leaves
// the field unset on the cluster descriptor and lets consumers fall back to what they did before.
//
// An invalid TT_HOST_ID throws rather than falling back to gethostname(): the variable is set on
// purpose, and quietly substituting a container hostname would produce a wrong-but-plausible
// topology, which is exactly what TT_HOST_ID exists to prevent. An unusable OS hostname only warns,
// because that is not something the operator asked for.
inline std::optional<std::string> local_host_id() {
    const std::optional<std::string> host_id_from_env = get_env_var_value(TT_HOST_ID_ENV.data());
    if (host_id_from_env.has_value()) {
        const std::string host_id = trim_whitespace(host_id_from_env.value());
        // Exported but empty is a launcher accident, not a request for an empty host id.
        if (!host_id.empty()) {
            validate_host_id(host_id, TT_HOST_ID_ENV);
            log_info(LogUMD, "Using host id \"{}\" from {}.", host_id, TT_HOST_ID_ENV);
            return host_id;
        }
        log_warning(LogUMD, "{} is set but empty, falling back to the OS hostname.", TT_HOST_ID_ENV);
    }

    std::array<char, 256> hostname = {};
    if (gethostname(hostname.data(), hostname.size() - 1) != 0) {
        log_warning(LogUMD, "gethostname() failed, leaving the host id unset. Set {} to provide one.", TT_HOST_ID_ENV);
        return std::nullopt;
    }

    // Stored raw, with no FQDN stripping -- consumers canonicalize.
    const std::string host_id(hostname.data());
    const std::optional<std::string> host_id_error = get_host_id_error(host_id);
    if (host_id_error.has_value()) {
        log_warning(
            LogUMD,
            "Leaving the host id unset because the OS hostname \"{}\" cannot be used as one: {}. Set {} to "
            "provide one.",
            host_id,
            host_id_error.value(),
            TT_HOST_ID_ENV);
        return std::nullopt;
    }
    log_debug(LogUMD, "Using host id \"{}\" from the OS hostname.", host_id);
    return host_id;
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
