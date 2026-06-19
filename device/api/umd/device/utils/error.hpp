// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

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
 * @brief Structured metadata for a host-side MMIO operation that overran its budget.
 *
 * Carries the timing and progress context of the timed-out transfer. operation is an owned
 * std::string, so callers may pass temporaries safely. The IO routine that timed out is
 * recoverable from the exception's captured stack trace, so it is not duplicated here.
 */
struct DeviceTimeoutData {
    std::string operation;                ///< The individual op that overran (e.g. "store").
    std::uint32_t op_bytes = 0;           ///< Size in bytes of the single op that overran.
    std::chrono::nanoseconds delta{0};    ///< Wall-clock time the op actually took.
    std::chrono::milliseconds budget{0};  ///< Configured per-op budget that was exceeded.
    std::size_t bytes_remaining = 0;      ///< Bytes still to transfer when the budget elapsed.
    std::size_t total_bytes = 0;          ///< Total bytes of the overall transfer.
};

/**
 * @brief Error raised when a host-side MMIO operation exceeds its configured wall-clock budget.
 *
 * Distinct from SigbusError: SigbusError indicates the platform's PCIe completion timeout fired
 * (a hardware-detected fault), whereas DeviceTimeoutError indicates a software budget elapsed —
 * typically because writes were piling up against a slow or dead NOC before SIGBUS could fire.
 * Keeping them separate lets callers branch on retry policy.
 */
struct DeviceTimeoutError : public UmdError<DeviceTimeoutData> {
    DeviceTimeoutError(
        const std::string& operation,
        std::uint32_t op_bytes,
        std::chrono::nanoseconds delta,
        std::chrono::milliseconds budget,
        std::size_t bytes_remaining,
        std::size_t total_bytes) :
        UmdError<DeviceTimeoutData>(
            format_message(operation, op_bytes, delta, budget, bytes_remaining, total_bytes),
            DeviceTimeoutData{operation, op_bytes, delta, budget, bytes_remaining, total_bytes}) {}

private:
    static std::string format_message(
        const std::string& operation,
        std::uint32_t op_bytes,
        std::chrono::nanoseconds delta,
        std::chrono::milliseconds budget,
        std::size_t bytes_remaining,
        std::size_t total_bytes) {
        // Report in microseconds: the budget is in whole ms but a sub-ms overrun (or a 0 ms budget)
        // would otherwise truncate to "took 0 ms", under-reporting the elapsed time for diagnostics.
        const auto delta_us = std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
        return "MMIO per-op timeout: " + std::to_string(op_bytes) + "B " + operation + " took " +
               std::to_string(delta_us) + " us (budget=" + std::to_string(budget.count()) + " ms), " +
               std::to_string(bytes_remaining) + " of " + std::to_string(total_bytes) + " bytes remaining.";
    }
};

}  // namespace tt::umd::error

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
