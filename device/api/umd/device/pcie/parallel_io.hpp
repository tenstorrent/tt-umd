/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once



#include <vector>
#include <thread>
#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <string>

#include "tlb_window.hpp"
#include "umd/device/pcie/tlb_handle.hpp"

namespace tt::umd {

class ParallelIO {
public:
    struct Range {
        uint64_t start;
        uint64_t size;
    };

    ParallelIO(
               tt_xy_
               uint64_t global_start,
               uint64_t global_size,
               size_t num_threads)
      : global_start_(global_start),
        global_size_(global_size),
        num_threads_(num_threads)
    {
        if (num_threads_ == 0) {
            throw std::invalid_argument("num_threads must be >=1");
        }

        split_ranges();
        create_windows();
    }

    ~ParallelIO() = default;

    void configure_all(const tlb_data& new_config) {
        for (auto& win : windows_) {
            win->configure(new_config);
        }
    }

    void parallel_write(uint64_t offset, const void* data, size_t size) {
        do_parallel_io(Operation::Write, offset, const_cast<void*>(data), size);
    }

    void parallel_read(uint64_t offset, void* data, size_t size) {
        do_parallel_io(Operation::Read, offset, data, size);
    }

    const std::vector<Range>& ranges() const { return ranges_; }
    size_t num_threads() const { return num_threads_; }

private:
    enum class Operation { Read, Write };

    uint64_t global_start_;
    uint64_t global_size_;
    size_t num_threads_;
    std::vector<Range> ranges_;
    std::vector<std::unique_ptr<TlbWindow>> windows_;

    void split_ranges() {
        ranges_.clear();
        uint64_t total = global_size_;
        uint64_t base_size = (num_threads_ == 0) ? 0 : (total / num_threads_);
        uint64_t rem = (num_threads_ == 0) ? 0 : (total % num_threads_);

        uint64_t cur = global_start_;
        for (size_t i = 0; i < num_threads_; ++i) {
            uint64_t this_size = base_size + (i < rem ? 1 : 0);
            ranges_.push_back(Range{cur, this_size});
            cur += this_size;
        }
    }

    static tlb_data make_tlb_for_range(uint64_t base, uint64_t size) {
        tlb_data d{};
        d.local_offset = base;
        d.x_start = base;
        d.x_end = (size == 0) ? base : (base + size - 1);
        d.y_start = 0;
        d.y_end = 0;
        d.noc_sel = 0;
        d.mcast = 0;
        d.ordering = tlb_data::Relaxed;
        d.linked = 0;
        d.static_vc = 0;
        return d;
    }

    void create_windows() {
        windows_.clear();
        windows_.reserve(num_threads_);
        for (size_t i = 0; i < num_threads_; ++i) {
            tlb_data cfg = make_tlb_for_range(ranges_[i].start, ranges_[i].size);
            windows_.push_back(std::make_unique<TlbWindow>(std::move(h), cfg));
        }
    }

    std::optional<std::string> do_parallel_io(Operation op, uint64_t offset, void* data, size_t size) {
        if (size == 0) return std::nullopt;
        if (offset < global_start_ || offset + size > global_start_ + global_size_) {
            return std::make_optional<std::string>("Requested range outside global range");
        }

        std::vector<std::future<std::optional<std::string>>> futures;
        futures.reserve(num_threads_);

        uint8_t* buf = static_cast<uint8_t*>(data);

        for (size_t tid = 0; tid < num_threads_; ++tid) {
            Range r = ranges_[tid];
            if (r.size == 0) {
                futures.push_back(std::async(std::launch::deferred, [](){ return std::optional<std::string>{}; }));
                continue;
            }

            uint64_t req_start = offset;
            uint64_t req_end = offset + size;
            uint64_t r_start = r.start;
            uint64_t r_end = r.start + r.size;

            uint64_t inter_start = std::max(req_start, r_start);
            uint64_t inter_end = std::min(req_end, r_end);

            if (inter_start >= inter_end) {
                futures.push_back(std::async(std::launch::deferred, [](){ return std::optional<std::string>{}; }));
                continue;
            }

            size_t local_offset = static_cast<size_t>(inter_start - req_start);
            size_t local_size = static_cast<size_t>(inter_end - inter_start);

            futures.push_back(std::async(std::launch::async,
                [this, tid, op, inter_start, local_size, local_offset, buf]() -> std::optional<std::string> {
                    TlbWindow& win = *windows_[tid];

                    try {
                        if (op == Operation::Write) {
                            win.write_block(inter_start, buf + local_offset, local_size);
                        } else {
                            win.read_block(inter_start, buf + local_offset, local_size);
                        }
                    } catch (const std::exception& ex) {
                        return std::make_optional<std::string>(
                            std::string("Thread ") + std::to_string(tid) + ": " + ex.what()
                        );
                    } catch (...) {
                        return std::make_optional<std::string>(
                            std::string("Thread ") + std::to_string(tid) + ": unknown error"
                        );
                    }

                    return std::optional<std::string>{};
                }
            ));
        }

        for (auto& f : futures) {
            std::optional<std::string> err = f.get();
            if (err.has_value()) {
                // report the error.
            }
        }
    }
};

} // namespace tt::umd