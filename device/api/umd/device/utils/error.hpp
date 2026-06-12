// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#define UMD_ERROR_HPP_INTERNAL_INCLUDE
#include "umd/device/utils/error_detail.hpp"
#undef UMD_ERROR_HPP_INTERNAL_INCLUDE

namespace tt::umd::error {

/**
 * @brief Empty struct used when an UmdError has no associated metadata.
 *
 * This type serves as a placeholder for UmdError template instantiations
 * that do not require additional data beyond the error message.
 */
struct NoData {};

/**
 * @brief Generic runtime error with no additional metadata.
 *
 * This error type is similar to std::runtime_error, providing only an error message
 * with no additional details. It can be used as a placeholder until a more concrete
 * error type is defined for a specific situation.
 */
struct RuntimeError : public UmdError<NoData> {
    explicit RuntimeError(const std::string& message) : UmdError<NoData>(message, {}) {}
};

/**
 * @brief Exception thrown when a SIGBUS signal is intercepted.
 * This indicates a hardware access error, likely due to a reset or
 * hanging device while accessing mapped memory.
 */
class SigbusError : public std::runtime_error {
public:
    explicit SigbusError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace tt::umd::error

namespace tt::umd {

inline void validate_register_access(uint64_t addr, size_t size) {
    if (addr % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Register address must be 4-byte aligned.");
    }
    if (size % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Register access size must be a multiple of 4 bytes.");
    }
}

}  // namespace tt::umd
