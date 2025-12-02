/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>

#include "umd/device/pcie/tlb_window.hpp"

namespace tt::umd {

// ------------------------- ThreadPool -------------------------
class ThreadPool {
public:
    explicit ThreadPool(size_t nthreads) : stop(false) {
        for (size_t i = 0; i < nthreads; i++) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> job;

                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) {
                            return;
                        }

                        job = std::move(tasks.front());
                        tasks.pop();
                    }
                    job();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }
        cv.notify_all();
        for (auto &t : workers) {
            t.join();
        }
    }

    template <class F>
    auto enqueue(F &&f) -> std::future<decltype(f())> {
        using Ret = decltype(f());

        auto task = std::make_shared<std::packaged_task<Ret()>>(std::forward<F>(f));
        std::future<Ret> res = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.emplace([task] { (*task)(); });
        }
        cv.notify_one();

        return res;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex mtx;
    std::condition_variable cv;
    bool stop;
};

// ------------------------- ParallelIO -------------------------
class ParallelIO {
public:
    static constexpr size_t TLB_WINDOW_SIZE = 2ull * 1024 * 1024;  // 2 MiB

    ParallelIO(size_t num_threads, tt_xy_pair core, uint64_t base_addr, uint64_t size, uint32_t fd) :
        nthreads_(num_threads), core_(core), base_addr_(base_addr), size_(size), fd_(fd), pool_(num_threads) {
        tlb_windows_.reserve(nthreads_);
        // tlb_data tlb_config{};
        // tlb_config.local_offset = base_addr_;
        // tlb_config.x_end = core_.x;
        // tlb_config.y_end = core_.y;
        // tlb_config.noc_sel = 0;
        // tlb_config.ordering = tlb_data::Relaxed;
        // tlb_config.static_vc = true;
        // size_t chunk = size / num_threads;
        for (size_t i = 0; i < nthreads_; i++) {
            tlb_windows_.emplace_back(std::make_unique<TlbWindow>(
                std::make_unique<TlbHandle>(fd_, TLB_WINDOW_SIZE, TlbMapping::WC), tlb_data{}));
            // tlb_config.local_offset += chunk;
        }
    }

    void read_from_device(void *host_buffer) {
        run_parallel_io([&](TlbWindow &win, uint64_t chunk_size, uint64_t host_offset) {
            std::cout << "Thread read" << std::endl;
            win.read_block_reconfigure(
                (uint8_t *)host_buffer + host_offset, core_, base_addr_ + host_offset, chunk_size, tlb_data::Relaxed);
        });
    }

    void write_to_device(const void *host_buffer) {
        run_parallel_io([&](TlbWindow &win, uint64_t chunk_size, uint64_t host_offset) {
             std::cout << "Thread write" << std::endl;
            win.write_block_reconfigure(
                (uint8_t *)host_buffer + host_offset, core_, base_addr_ + host_offset, chunk_size, tlb_data::Relaxed);
        });
    }

private:
    template <typename Func>
    void run_parallel_io(Func &&func) {
        std::vector<std::future<void>> futures;

        uint64_t remaining = size_;
        uint64_t host_offset = 0;

        uint64_t chunk = size_ / nthreads_;

        size_t idx = 0;
        while (remaining > 0 && idx < nthreads_) {
            // uint64_t chunk = std::min<uint64_t>(remaining, TLB_WINDOW_SIZE);

            futures.push_back(
                pool_.enqueue([&, idx, chunk, host_offset] { func(*tlb_windows_[idx], chunk, host_offset); }));
            remaining -= chunk;
            host_offset += chunk;
            idx++;
        }

        // Wait for all workers
        for (auto &f : futures) {
            f.get();
        }
    }

private:
    size_t nthreads_;
    tt_xy_pair core_;
    uint64_t base_addr_;
    uint64_t size_;
    uint32_t fd_;
    ThreadPool pool_;
    std::vector<std::unique_ptr<TlbWindow>> tlb_windows_;
};

}  // namespace tt::umd
