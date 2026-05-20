// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
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

/**
 * @brief Exception thrown when a host-side MMIO operation exceeds its
 * configured wall-clock budget.
 *
 * Distinct from SigbusError: SigbusError indicates the platform's PCIe
 * completion timeout fired (hardware-detected fault). DeviceTimeoutError
 * indicates a software budget elapsed — typically because writes were
 * piling up against a slow or dead NOC before SIGBUS could fire.
 *
 * Fields are stored as-is and the formatted what() string is built lazily
 * on first call. function_name() and operation() are expected to point at
 * string literals (callers in the memcpy path pass "memcpy_to_device" /
 * "store" etc.) — the exception does not copy them.
 */
class DeviceTimeoutError : public std::exception {
public:
    DeviceTimeoutError(
        const char* function_name,
        const char* operation,
        std::uint32_t op_bytes,
        std::chrono::nanoseconds delta,
        std::chrono::milliseconds budget,
        std::size_t bytes_remaining,
        std::size_t total_bytes) noexcept :
        function_name_(function_name),
        operation_(operation),
        op_bytes_(op_bytes),
        delta_(delta),
        budget_(budget),
        bytes_remaining_(bytes_remaining),
        total_bytes_(total_bytes) {}

    const char* what() const noexcept override {
        if (message_.empty()) {
            try {
                auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta_).count();
                message_ =
                    std::string(function_name_) + " per-op timeout: " + std::to_string(op_bytes_) + "B " + operation_ +
                    " took " + std::to_string(delta_ms) + " ms (budget=" + std::to_string(budget_.count()) + " ms), " +
                    std::to_string(bytes_remaining_) + " of " + std::to_string(total_bytes_) + " bytes remaining.";
            } catch (...) {
                return "tt::umd::error::DeviceTimeoutError (message formatting failed).";
            }
        }
        return message_.c_str();
    }

    const char* function_name() const noexcept { return function_name_; }

    const char* operation() const noexcept { return operation_; }

    std::uint32_t op_bytes() const noexcept { return op_bytes_; }

    std::chrono::nanoseconds delta() const noexcept { return delta_; }

    std::chrono::milliseconds budget() const noexcept { return budget_; }

    std::size_t bytes_remaining() const noexcept { return bytes_remaining_; }

    std::size_t total_bytes() const noexcept { return total_bytes_; }

private:
    const char* function_name_;
    const char* operation_;
    std::uint32_t op_bytes_;
    std::chrono::nanoseconds delta_;
    std::chrono::milliseconds budget_;
    std::size_t bytes_remaining_;
    std::size_t total_bytes_;
    mutable std::string message_;
};

}  // namespace tt::umd::error
