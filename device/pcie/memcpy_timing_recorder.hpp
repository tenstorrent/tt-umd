// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "umd/device/utils/mmio_timeout_config.hpp"

namespace tt::umd {

// An anomalous op: one that exceeded the per-op budget but was vetoed as a false alarm by
// on_timeout, so the transfer continued instead of throwing.
struct SlowOp {
    std::size_t op_index;
    std::int64_t ns;
};

constexpr std::size_t MAX_SLOW_OPS = 16;

// RAII per-call timing recorder. Tracks running min/max/mean/total over every op via record(), plus
// a small fixed-size buffer of anomalous (over-budget) ops, and dumps the aggregate summary on
// destruction. Dumping from the destructor means a transfer that aborts via a timeout throw still
// reports its summary, including the stalling op if it was vetoed rather than thrown.
class MemcpyTimingRecorder {
public:
    MemcpyTimingRecorder(const char* fn_name, std::size_t total_size, bool enabled) :
        fn_name_(fn_name),
        total_size_(total_size),
        enabled_(enabled),
        budget_ns_(std::chrono::duration_cast<std::chrono::nanoseconds>(MmioTimeoutConfig::get_op_timeout()).count()) {}

    MemcpyTimingRecorder(const MemcpyTimingRecorder&) = delete;
    MemcpyTimingRecorder& operator=(const MemcpyTimingRecorder&) = delete;

    ~MemcpyTimingRecorder() noexcept {
        if (!enabled_) {
            return;
        }

        dump();
    }

    void record(std::chrono::steady_clock::time_point start, std::uint32_t bytes) noexcept {
        if (!enabled_) {
            return;
        }
        const auto delta =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
        const std::int64_t ns = delta.count();

        const std::size_t op_index = op_count_++;
        min_ns_ = std::min(min_ns_, ns);
        max_ns_ = std::max(max_ns_, ns);
        total_ns_ += ns;

        if (budget_ns_ != 0 && ns > budget_ns_) {
            if (slow_op_count_ < slow_ops_.size()) {
                slow_ops_[slow_op_count_++] = {op_index, ns};
            } else {
                ++slow_ops_dropped_;
            }
        }
    }

private:
    // Emits one line to stderr: function name, total size, op count, min/max/mean/total ns, and the
    // list of anomalous (over-budget) ops.
    void dump() const noexcept {
        if (op_count_ == 0) {
            return;
        }

        std::array<char, 512> buf;
        char* const limit =
            buf.data() + buf.size() -
            1;  // The last byte is reserved so the trailing '\n' can always be appended without a bounds check.
        auto remaining = [&](char* end) { return static_cast<std::size_t>(limit - end); };

        char* end = fmt::format_to_n(
                        buf.data(),
                        remaining(buf.data()),
                        "[{}] size={} ops={} min={}ns max={}ns mean={}ns total={}ns",
                        fn_name_,
                        total_size_,
                        op_count_,
                        min_ns_,
                        max_ns_,
                        total_ns_ / static_cast<std::int64_t>(op_count_),
                        total_ns_)
                        .out;

        if (slow_op_count_ > 0) {
            end = fmt::format_to_n(end, remaining(end), " slow_ops=[").out;
            for (std::size_t i = 0; i < slow_op_count_; ++i) {
                end =
                    fmt::format_to_n(
                        end, remaining(end), "{}op#{}:{}ns", i == 0 ? "" : ",", slow_ops_[i].op_index, slow_ops_[i].ns)
                        .out;
            }
            if (slow_ops_dropped_ > 0) {
                end = fmt::format_to_n(end, remaining(end), ",+{} dropped", slow_ops_dropped_).out;
            }
            end = fmt::format_to_n(end, remaining(end), "]").out;
        }
        *end++ = '\n';

        std::fwrite(buf.data(), 1, end - buf.data(), stderr);
    }

    const char* fn_name_;
    std::size_t total_size_;
    bool enabled_;
    std::int64_t budget_ns_;

    std::size_t op_count_ = 0;
    std::int64_t min_ns_ = std::numeric_limits<std::int64_t>::max();
    std::int64_t max_ns_ = std::numeric_limits<std::int64_t>::min();
    std::int64_t total_ns_ = 0;

    std::array<SlowOp, MAX_SLOW_OPS> slow_ops_{};
    std::size_t slow_op_count_ = 0;
    std::size_t slow_ops_dropped_ = 0;
};

}  // namespace tt::umd
