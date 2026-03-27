// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cxxabi.h>
#include <execinfo.h>

#include <exception>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

namespace error {
static inline std::string demangle(const char* str) {
    int status = 0;
    std::string rt(256, '\0');
    if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", rt.data())) {
        size_t length = 0;
        std::unique_ptr<char, decltype(&free)> v(abi::__cxa_demangle(rt.data(), nullptr, &length, &status), &free);
        if (v) {
            return std::string(v.get());
        }
    }
    return str;
}

/**
 * @brief Get the current call stack
 * @param[out] bt Save Call Stack
 * @param[in] size Maximum number of return layers
 * @param[in] skip Skip the number of layers at the top of the stack
 */
inline std::vector<std::string> backtrace(int size = 64, int skip = 1, void* caller_address = nullptr) {
    std::vector<std::string> bt;
    std::vector<void*> array(size);

    if (caller_address != nullptr) {
        array.at(1) = caller_address;
    }

    size_t s = ::backtrace(array.data(), size);
    std::unique_ptr<char*, decltype(&free)> strings(backtrace_symbols(array.data(), s), &free);

    if (strings == nullptr) {
        return bt;
    }

    for (size_t i = skip; i < s; ++i) {
        bt.push_back(demangle(strings.get()[i]));
    }

    return bt;
}

inline std::string backtrace_to_string(
    int size = 64, int skip = 2, const std::string& prefix = "", void* caller_address = nullptr) {
    std::vector<std::string> bt = backtrace(size, skip, caller_address);
    std::stringstream ss;
    for (size_t i = 0; i < bt.size(); ++i) {
        ss << prefix << bt[i] << std::endl;
    }
    return ss.str();
}
}  // namespace error

template <typename DATA_T>
class UmdError {
public:
    explicit UmdError(const std::string& what, const DATA_T& data) : what_string_(what), exception_data_(data) {}

    std::string& what() { return what_string_; }

    const std::string& what() const noexcept { return what_string_; }

    DATA_T& data() { return exception_data_; }

    const DATA_T& data() const noexcept { return exception_data_; }

private:
    std::string what_string_;
    DATA_T exception_data_;
};

template <typename ERROR_T>
class UmdException : public std::runtime_error {
public:
    explicit UmdException(ERROR_T error, const std::string& file = "", uint32_t line = 0) :
        std::runtime_error(error.what()), error_(error), file_(file), line_(line) {
        backtrace_ = tt::umd::error::backtrace();
    }

    std::string& what() { return error_.what(); }

    const char* what() const noexcept override {
        std::stringstream ss;
        ss << error_.what() << std::endl;
        ss << "Location: " << file_ << ":" << line_ << std::endl;
        for (size_t i = 0; i < backtrace_.size(); ++i) {
            ss << backtrace_[i] << std::endl;
        }
        output_ = ss.str();
        return output_.c_str();
    }

    ERROR_T& error() { return error_; }

    const ERROR_T& error() const noexcept { return error_; }

protected:
    ERROR_T error_;
    std::string file_;
    uint32_t line_ = 0;
    std::vector<std::string> backtrace_;
    mutable std::string output_;
};

#define UMD_THROW(error_type, ...) \
    (throw tt::umd::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__))

#define UMD_THROW_IF(condition, error_type, ...)                                                        \
    ((condition) ? throw tt::umd::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__) \
                 : error_type(__VA_ARGS__))

//-----------------------------------------------------------------------------

struct CoreExceptionData {
public:
    xy_pair core;
};

struct ETHHeartbeatFailureData : public CoreExceptionData {
    uint32_t postcode;
    uint32_t heartbeat_value;
};

class ETHHeartbeatError : public UmdError<ETHHeartbeatFailureData> {
public:
    explicit ETHHeartbeatError(tt_xy_pair eth_core, uint32_t postcode, uint32_t heartbeat_value);
};

//-----------------------------------------------------------------------------
/**
 * @brief Exception thrown when a SIGBUS signal is intercepted.
 * This indicates a hardware access error, likely due to a reset or
 * hanging device while accessing mapped memory.
 */
class SigbusError : public std::runtime_error {
public:
    explicit SigbusError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace tt::umd
