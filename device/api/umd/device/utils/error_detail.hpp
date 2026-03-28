// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cxxabi.h>
#include <execinfo.h>

#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tt::umd::error {
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

template <typename DATA_T>
class UmdError {
public:
    explicit UmdError(const std::string& what, const DATA_T& data) : message_(what), error_data_(data) {}

    std::string& message() { return message_; }

    const std::string& message() const noexcept { return message_; }

    DATA_T& data() { return error_data_; }

    const DATA_T& data() const noexcept { return error_data_; }

private:
    std::string message_;
    DATA_T error_data_;
};

template <typename ERROR_T>
class UmdException : public std::runtime_error {
public:
    explicit UmdException(ERROR_T error, const std::string& file = "", uint32_t line = 0) :
        std::runtime_error(error.message()), line_(line), file_(file), error_(error) {
        backtrace_ = tt::umd::error::backtrace();
        std::stringstream ss;
        ss << error_.message() << std::endl;
        ss << "Location: " << file_ << ":" << line_ << std::endl;
        for (size_t i = 0; i < backtrace_.size(); ++i) {
            ss << backtrace_[i] << std::endl;
        }
        what_output_ = ss.str();
    }

    const char* what() const noexcept override { return what_output_.c_str(); }

    ERROR_T& error() { return error_; }

    const ERROR_T& error() const noexcept { return error_; }

    const std::string& file() const noexcept { return file_; }

    uint32_t line() const noexcept { return line_; }

    const std::vector<std::string>& backtrace() const noexcept { return backtrace_; }

protected:
    uint32_t line_ = 0;
    std::string file_;
    std::vector<std::string> backtrace_;
    std::string what_output_;
    ERROR_T error_;
};

#define UMD_THROW(error_type, ...) \
    (throw tt::umd::error::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__))

#define UMD_THROW_IF(condition, error_type, ...)                                                               \
    ((condition) ? throw tt::umd::error::UmdException<error_type>(error_type(__VA_ARGS__), __FILE__, __LINE__) \
                 : error_type(__VA_ARGS__))

}  // namespace tt::umd::error
