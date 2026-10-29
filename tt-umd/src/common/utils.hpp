// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <unordered_set>

#include "tt-umd-utils/internal/host.hpp"
#include "tt-umd-utils/internal/string_utils.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd::utils {

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

// A cluster id has to fit in the fixed 128-byte buffer that tt-metal packs it into, which is not
// NUL-terminated -- hence 128 and not 127.
inline constexpr size_t CLUSTER_ID_MAX_LENGTH = 128;

// Returns why cluster_id is not a legal cluster id, or nullopt when it is legal. Legal ids are 1 to
// CLUSTER_ID_MAX_LENGTH characters from [A-Za-z0-9._-]. That charset is deliberately
// hostname-shaped: cluster ids are currently hostname-valued and have to join against hostnames in
// the factory system descriptor.
inline std::optional<std::string> get_cluster_id_error(const std::string& cluster_id) {
    if (cluster_id.empty()) {
        return "it is empty";
    }
    if (cluster_id.size() > CLUSTER_ID_MAX_LENGTH) {
        return fmt::format("it is {} characters long, the limit is {}", cluster_id.size(), CLUSTER_ID_MAX_LENGTH);
    }
    for (const char character : cluster_id) {
        const unsigned char c = static_cast<unsigned char>(character);
        const bool is_alphanumeric = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!is_alphanumeric && character != '.' && character != '-' && character != '_') {
            return fmt::format("it contains '{}', and only alphanumerics, '.', '-' and '_' are allowed", character);
        }
    }
    return std::nullopt;
}

inline void validate_cluster_id(const std::string& cluster_id, const std::string_view source) {
    const std::optional<std::string> cluster_id_error = get_cluster_id_error(cluster_id);
    if (cluster_id_error.has_value()) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Invalid cluster id \"{}\" from {}: {}.", cluster_id, source, cluster_id_error.value()));
    }
}

// Cluster id to stamp on a cluster descriptor built by discovery: the caller-supplied one when there
// is one, otherwise the OS hostname. Returns nullopt when there is no supplied id and the hostname
// cannot be used as one, which leaves the field unset and lets consumers fall back to what they did
// before.
//
// A supplied id that is not legal throws: the caller passed it on purpose, and quietly substituting
// the hostname would produce a wrong-but-plausible topology, which is what supplying an id is meant
// to prevent. An unusable hostname only warns, because that is not something the caller asked for.
inline std::optional<std::string> resolve_cluster_id(const std::optional<std::string>& supplied_cluster_id) {
    static constexpr std::string_view SOURCE = "TopologyDiscoveryOptions::cluster_id";

    if (supplied_cluster_id.has_value()) {
        validate_cluster_id(supplied_cluster_id.value(), SOURCE);
        log_debug(LogUMD, "Using cluster id \"{}\" from {}.", supplied_cluster_id.value(), SOURCE);
        return supplied_cluster_id;
    }

    std::array<char, 256> hostname = {};
    if (gethostname(hostname.data(), hostname.size() - 1) != 0) {
        log_warning(LogUMD, "gethostname() failed, leaving the cluster id unset. Pass one in {}.", SOURCE);
        return std::nullopt;
    }

    // Stored raw, with no FQDN stripping -- consumers canonicalize.
    std::string cluster_id(hostname.data());
    const std::optional<std::string> cluster_id_error = get_cluster_id_error(cluster_id);
    if (cluster_id_error.has_value()) {
        log_warning(
            LogUMD,
            "Leaving the cluster id unset because the OS hostname \"{}\" cannot be used as one: {}. Pass one in {}.",
            cluster_id,
            cluster_id_error.value(),
            SOURCE);
        return std::nullopt;
    }
    log_debug(LogUMD, "Using cluster id \"{}\" from the OS hostname.", cluster_id);
    return cluster_id;
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

inline void validate_register_access(
    uint64_t addr, size_t size, RegisterAccessWidth width = RegisterAccessWidth::BITS32) {
    const size_t access_size = register_access_size(width);
    if (addr % access_size != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Register address must be {}-byte aligned.", access_size));
    }
    if (size % access_size != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Register access size must be {}-byte aligned.", access_size));
    }
}

// Only Quasar has 64-bit registers; Wormhole and Blackhole accept nothing but 32-bit register accesses.
inline void validate_register_access_width(tt::ARCH arch, RegisterAccessWidth width) {
    if (width == RegisterAccessWidth::BITS64 && arch != tt::ARCH::QUASAR) {
        UMD_THROW(
            error::RuntimeError, fmt::format("64-bit register accesses are not supported on {}.", arch_to_str(arch)));
    }
}

}  // namespace tt::umd
