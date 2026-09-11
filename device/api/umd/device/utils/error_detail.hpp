// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// IWYU pragma: private, include "umd/device/utils/error.hpp"

#ifndef UMD_ERROR_HPP_INTERNAL_INCLUDE
#error "error_detail.hpp is a private header. Include umd/device/utils/error.hpp instead."
#endif
#include <cxxabi.h>
#include <execinfo.h>

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * error_detail.hpp contains the UmdError and UmdException classes.
 * The classes were designed carefully to conform to the following design goals:
 * - Must be C++17.
 * - No 3rd party dependencies.
 * - Errors are easy to define, construct and it's easy to access their metadata.
 * - Errors can easily be converted to exceptions.
 * - Once exceptions will be removed in UMD, it is easy to convert them back to
 *   errors by just removing the exception layer that was wrapping them.
 * - Exceptions automatically capture stack trace, file and line information.
 * - Exceptions display stack trace, file and line information through their interface.
 */

namespace tt::umd::error {
/**
 * @brief Captures and demangles the current stack trace.
 *
 * This function uses backtrace() to capture the call stack and demangles C++ symbol names
 * for better readability. It's useful for debugging and error reporting.
 *
 * @param max_frames Maximum number of stack frames to capture (default: 64).
 * @param skip Number of top stack frames to skip (default: 1, skipping this function itself).
 * @return Vector of demangled stack frame strings, empty if capture fails.
 */
static inline std::vector<std::string> get_stacktrace(uint32_t max_frames = 64, uint32_t skip = 1) {
    if (skip >= max_frames) {
        return std::vector<std::string>{};
    }

    std::vector<std::string> stack_frames;
    std::vector<void*> target_stack(max_frames);
    int addr_count = backtrace(target_stack.data(), max_frames);

    if (addr_count <= skip) {
        return stack_frames;
    }

    std::unique_ptr<char*, void (*)(void*)> symbols(backtrace_symbols(target_stack.data(), addr_count), std::free);

    if (!symbols) {
        return stack_frames;
    }
    for (int i = skip; i < addr_count; i++) {
        std::string entry(symbols.get()[i]);

        size_t open_paren = entry.find('(');
        if (open_paren != std::string::npos) {
            size_t plus_sign = entry.find('+', open_paren);
            if (plus_sign != std::string::npos) {
                std::string mangled = entry.substr(open_paren + 1, plus_sign - open_paren - 1);

                // Skip empty mangled names (common in some shared libs/main).
                if (mangled.empty()) {
                    stack_frames.push_back(entry);
                    continue;
                }

                int status;
                std::unique_ptr<char, void (*)(void*)> demangled(
                    abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status), std::free);

                if (status == 0 && demangled != nullptr) {
                    stack_frames.push_back(demangled.get());
                } else {
                    stack_frames.push_back(mangled);
                }
            }
        } else {
            stack_frames.push_back(entry);
        }
    }

    return stack_frames;
}

/**
 * @brief Error object that pairs an error message with structured error data.

 * This template class represents an error condition in UMD. The interface
 * contains a human-readable error message and user-defined structured error
 * metadata of type DATA_T.
 *
 * @tparam DATA_T Type of the structured error data.
 */
template <typename DATA_T>
class UmdError {
public:
    /**
     * @brief Constructs an error with a message and associated data.
     *
     * @param what Human-readable error message.
     * @param data Structured error data providing additional context.
     */
    explicit UmdError(const std::string& what, const DATA_T& data) : message_(what), error_data_(data) {}

    /**
     * @brief Gets a mutable reference to the error message.
     *
     * @return Reference to the error message string.
     */
    std::string& message() noexcept { return message_; }

    /**
     * @brief Gets a const reference to the error message.
     *
     * @return Const reference to the error message string.
     */
    const std::string& message() const noexcept { return message_; }

    /**
     * @brief Gets a mutable reference to the structured error data.
     *
     * @return Reference to the error data.
     */
    DATA_T& data() noexcept { return error_data_; }

    /**
     * @brief Gets a const reference to the structured error data.
     *
     * @return Const reference to the error data.
     */
    const DATA_T& data() const noexcept { return error_data_; }

private:
    std::string message_;  ///< Human-readable error message.
    DATA_T error_data_;    ///< Structured error data.
};

class UmdBaseException : public std::runtime_error {
public:
    explicit UmdBaseException(const std::string& what) : std::runtime_error(what) {}

    /**
     * @brief This returns an error message string without the stack trace,
     * which has to be present in the overriden what() method for the
     * automatic stack trace.
     *
     * @return Simple error message string.
     */
    const char* message() const noexcept { return std::runtime_error::what(); }
};

/**
 * @brief Exception wrapper that adds location and stack trace information to UmdError.
 *
 * This template class wraps a UmdError object and provides standard exception functionality
 * through std::runtime_error. It captures the file location, line number, and stack trace
 * when the exception is constructed, making it easier to diagnose error conditions.
 *
 * @tparam ERROR_T Type of the UmdError object being wrapped (e.g., UmdError<ETHHeartbeatFailureData>).
 */
template <typename ERROR_T>
class UmdException : public UmdBaseException {
public:
    /**
     * @brief Constructs an exception with error details, location, and stack trace.
     *
     * Captures the current stack trace and formats a comprehensive error message
     * including the error message, source location, and backtrace.
     *
     * @param error The UmdError object containing the error message and data.
     * @param file Source file where the exception was thrown (typically __FILE__).
     * @param line Line number where the exception was thrown (typically __LINE__).
     * @param condition Assert condition that failed, causing the exception.
     */
    explicit UmdException(
        ERROR_T error, const std::string& file = "", uint32_t line = 0, const std::string& condition = "") :
        UmdBaseException(error.message()), line_(line), file_(file), condition_(condition), error_(error) {
        // Automatically capture stack trace on construction.
        // Skip first two frames (get_stacktrace() and this constructor.).
        backtrace_ = tt::umd::error::get_stacktrace(/*max_frames=*/64, /*skip=*/2);
        std::stringstream ss;
        ss << error_.message() << std::endl;
        ss << "Location: " << file_ << ":" << line_ << std::endl;
        if (!condition_.empty()) {
            ss << "Condition: " << condition_ << std::endl;
        }

        for (size_t i = 0; i < backtrace_.size(); ++i) {
            ss << std::setw(2) << std::right << i + 1 << ". " << backtrace_[i] << std::endl;
        }
        what_output_ = ss.str();

        /*
            Example stack trace output:
                Location: /tt-umd/device/topology/topology_discovery.cpp:491
                1. tt::umd::TopologyDiscovery::verify_fw_bundle_version(tt::umd::TTDevice*)
                2. tt::umd::TopologyDiscovery::get_connected_devices()
                3. tt::umd::TopologyDiscovery::create_ethernet_map()
                4. tt::umd::TopologyDiscovery::discover(tt::umd::TopologyDiscoveryOptions const&, tt::umd::IODeviceType,
            std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)
                5. tt::umd::Cluster::create_cluster_descriptor(std::__cxx11::basic_string<char, std::char_traits<char>,
            std::allocator<char> > const&, tt::umd::IODeviceType, tt::umd::TopologyDiscoveryOptions const&)
                6. ./build/tools/umd/topology(+0xbb0e4) [0x55c2986cc0e4]
                7. /lib/x86_64-linux-gnu/libc.so.6(+0x29d90) [0x7f341a06cd90]
                8. __libc_start_main
                9. ./build/tools/umd/topology(+0xb9975) [0x55c2986ca975]
        */
    }

    /**
     * @brief Returns a detailed error message including location and stack trace.
     *
     * @return C-string containing the full error message with diagnostic information.
     */
    const char* what() const noexcept override { return what_output_.c_str(); }

    /**
     * @brief Gets a const reference to the wrapped error object.
     *
     * @return Const reference to the UmdError object.
     */
    const ERROR_T& error() const noexcept { return error_; }

    /**
     * @brief Gets the source file where the exception was thrown.
     *
     * @return Const reference to the filename string.
     */
    const std::string& file() const noexcept { return file_; }

    /**
     * @brief Gets the line number where the exception was thrown.
     *
     * @return Line number in the source file.
     */
    uint32_t line() const noexcept { return line_; }

    /**
     * @brief Gets the assert condition linked with the exception.
     *
     * @return Const reference to the condition string.
     */
    const std::string& condition() const noexcept { return condition_; }

    /**
     * @brief Gets the captured stack trace.
     *
     * @return Const reference to vector of demangled stack frame strings.
     */
    const std::vector<std::string>& backtrace() const noexcept { return backtrace_; }

private:
    uint32_t line_ = 0;                   ///< Line number where exception was thrown.
    std::string file_;                    ///< Source file where exception was thrown.
    std::vector<std::string> backtrace_;  ///< Captured and demangled stack trace.
    std::string what_output_;             ///< Formatted error message with all details.
    std::string condition_;               ///< Optional assert condition.
    ERROR_T error_;                       ///< Wrapped UmdError object.
};

/**
 * @brief Macro to throw a UmdException with automatic location tracking.
 *
 * This macro constructs an error object of the specified type with the given arguments,
 * wraps it in a UmdException, and throws it. The file and line information are automatically
 * captured at the throw site.
 *
 * @param error_type The type of UmdError to construct (e.g., UmdError<ETHHeartbeatFailureData>).
 * @param ... Arguments to forward to the error_type constructor.
 *
 */
#define UMD_THROW(error_type, ...)                                                                   \
    do {                                                                                             \
        throw tt::umd::error::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__); \
    } while (0)

/**
 * @brief Macro to assert a condition and throw a UmdException if it fails.
 *
 * This macro evaluates a condition and throws a UmdException if the condition is false.
 * The file and line information are automatically captured at the assertion site.
 *
 * @param condition Boolean expression; exception is thrown if false.
 * @param error_type The type of UmdError to construct (e.g., UmdError<ETHHeartbeatFailureData>).
 * @param ... Arguments to forward to the error_type constructor.
 *
 * Example:
 * @code
 * UMD_ASSERT(ptr != nullptr, UmdError<NullPointerData>, "Pointer must not be null", data);
 * @endcode
 */
#define UMD_ASSERT(condition, error_type, ...)                                                                       \
    do {                                                                                                             \
        if (!(condition)) { /* NOLINT(readability-simplify-boolean-expr) */                                          \
            throw tt::umd::error::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__, #condition); \
        }                                                                                                            \
    } while (0)

/**
 * @brief Macro to either throw an UmdException or return an UmdError.
 *
 * This macro evaluates a condition and throws a UmdException if the condition is true.
 * If the condition is false, it returns the constructed error object without throwing.
 * The file and line information are automatically captured at the invocation site.
 *
 * @param condition Boolean expression; exception is thrown if true.
 * @param error_type The type of UmdError to construct (e.g., UmdError<ETHHeartbeatFailureData>).
 * @param ... Arguments to forward to the error_type constructor.
 *
 * Example:
 * @code
 * UMD_THROW_OR_RETURN(heartbeat_timeout, UmdError<ETHHeartbeatFailureData>, "Timeout", data);
 * @endcode
 */
#define UMD_THROW_OR_RETURN(condition, error_type, ...)                                                        \
    ((condition) ? throw tt::umd::error::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__) \
                 : error_type(__VA_ARGS__))

}  // namespace tt::umd::error
