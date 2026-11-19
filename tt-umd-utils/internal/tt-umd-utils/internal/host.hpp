// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace tt::umd::utils {

inline std::optional<std::string> get_env_var_value(const char* env_var_name) {
    const char* env_var = std::getenv(env_var_name);
    if (!env_var) {
        return std::nullopt;
    }
    return std::string(env_var);
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

constexpr bool is_arm_platform() {
#if defined(__aarch64__) || defined(__arm__)
    return true;
#else
    return false;
#endif
}

// ARM CPU implementer ID for Ampere custom cores (AmpereOne family).
constexpr const char* ARM_CPU_IMPLEMENTER_KEY = "CPU implementer";
constexpr const char* ARM_CPU_IMPLEMENTER_AMPEREONE = "0xc0";

// Check whether the CPU is an Ampere custom core (AmpereOne family, implementer 0xc0).
// Returns false for Ampere Altra (which uses ARM Neoverse N1 reference cores, implementer 0x41)
// and for non-ARM platforms.  Reads /proc/cpuinfo once.
inline bool is_ampereone() {
#if defined(__aarch64__) || defined(__arm__)
    static const bool result = [] {
        std::ifstream f("/proc/cpuinfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.find(ARM_CPU_IMPLEMENTER_KEY) != std::string::npos &&
                line.find(ARM_CPU_IMPLEMENTER_AMPEREONE) != std::string::npos) {
                return true;
            }
        }
        return false;
    }();
    return result;
#else
    return false;
#endif
}

}  // namespace tt::umd::utils
