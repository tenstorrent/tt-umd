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
static inline std::vector<std::string> get_stacktrace(int max_frames = 64, int skip = 1) {
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
        size_t plus_sign = entry.find('+', open_paren);

        if (open_paren != std::string::npos && plus_sign != std::string::npos) {
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
        } else {
            stack_frames.push_back(entry);
        }
    }

    return stack_frames;
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
        backtrace_ = tt::umd::error::get_stacktrace();
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
