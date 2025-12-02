/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "umd/device/pcie/tlb_window.hpp"

namespace tt::umd {

class ParallelIO {
public:
    static constexpr size_t TLB_WINDOW_SIZE = 1ull * 1024 * 1024;

    ParallelIO(size_t nthreads, tt_xy_pair core, uint64_t base_addr, uint64_t size, uint32_t fd) :
        nthreads_(nthreads), core_(core), base_addr_(base_addr), size_(size), fd_(fd), jobs_(nthreads) {
        // detect NUMA node of the main thread
        numa_node_ = detect_current_numa_node();
        fprintf(stderr, "ParallelIO: detected NUMA node %d\n", numa_node_);

        tlb_windows_.reserve(nthreads_);
        workers_.reserve(nthreads_);

        for (size_t i = 0; i < nthreads_; i++) {
            tlb_windows_.emplace_back(std::make_unique<TlbWindow>(
                std::make_unique<TlbHandle>(fd_, TLB_WINDOW_SIZE, TlbMapping::WC), tlb_data{}));

            workers_.emplace_back(&ParallelIO::worker_loop, this, i);
        }
    }

    ~ParallelIO() {
        stopping_.store(true, std::memory_order_release);

        for (size_t i = 0; i < nthreads_; i++) {
            jobs_[i].pending.store(true, std::memory_order_release);
        }

        cv_.notify_all();

        for (auto &t : workers_) {
            t.join();
        }
    }

    void read_from_device(void *host) { dispatch_io(host, false); }

    void write_to_device(const void *host) { dispatch_io(const_cast<void *>(host), true); }

private:
    struct Job {
        std::atomic<bool> pending{false};
        uint64_t chunk_size{0};
        uint64_t host_offset{0};
        bool is_write{false};
        void *host_ptr{nullptr};
    };

    // ----------------- NUMA Utilities -----------------
    static int detect_current_numa_node() {
#if defined(__linux__)
        int cpu = sched_getcpu();
        if (cpu < 0) {
            return 0;
        }

        // read /sys/devices/system/cpu/cpuX/nodeY
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/node%d", cpu, cpu);  // check node symlink
        DIR *d = opendir("/sys/devices/system/node/");
        if (!d) {
            return 0;  // fallback node 0
        }
        closedir(d);

        // simpler: assume cpu 0–N/2 node 0, N/2–N node1
        // best-effort fallback
        return cpu < (std::thread::hardware_concurrency() / 2) ? 0 : 1;
#else
        return 0;  // non-linux fallback
#endif
    }

    static void pin_thread_to_numa_node(int node) {
#if defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        // select first available core in the node
        // read cores from /sys/devices/system/node/nodeX/cpuY
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/node/node%d/cpu0", node);
        // For simplicity, pin to first core in node (could be improved)
        CPU_SET(node * (std::thread::hardware_concurrency() / 2), &cpuset);

        pthread_t current = pthread_self();
        int rc = pthread_setaffinity_np(current, sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            fprintf(stderr, "ParallelIO: pin_thread_to_numa_node failed (node=%d): %s\n", node, strerror(rc));
        }
#endif
    }

    // ----------------- Worker Loop -----------------
    void worker_loop(size_t id) {
        // pin to the NUMA node
        pin_thread_to_numa_node(numa_node_);

        TlbWindow &win = *tlb_windows_[id];

        for (;;) {
            Job &job = jobs_[id];

            // wait for job or stop
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [&] {
                    return job.pending.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire);
                });
            }

            if (stopping_.load(std::memory_order_acquire)) {
                return;
            }

            uint64_t dev_addr = base_addr_ + job.host_offset;

            if (job.is_write) {
                win.write_block_reconfigure(
                    (uint8_t *)job.host_ptr + job.host_offset, core_, dev_addr, job.chunk_size, tlb_data::Relaxed);
            } else {
                win.read_block_reconfigure(
                    (uint8_t *)job.host_ptr + job.host_offset, core_, dev_addr, job.chunk_size, tlb_data::Relaxed);
            }

            job.pending.store(false, std::memory_order_release);

            // signal completion
            int old = completed_.fetch_add(1, std::memory_order_acq_rel);
            if (old + 1 == n_active_jobs_) {
                std::lock_guard<std::mutex> lock(mtx_done_);
                cv_done_.notify_one();
            }
        }
    }

    // ----------------- Dispatch IO -----------------
    void dispatch_io(void *host_buf, bool is_write) {
        completed_.store(0, std::memory_order_release);

        uint64_t remaining = size_;
        uint64_t host_offset = 0;
        const uint64_t chunk = size_ / nthreads_;

        n_active_jobs_ = std::min<size_t>(nthreads_, (size_ + chunk - 1) / chunk);

        for (size_t i = 0; i < n_active_jobs_; i++) {
            Job &j = jobs_[i];

            j.chunk_size = (remaining < chunk) ? remaining : chunk;
            j.host_offset = host_offset;
            j.host_ptr = host_buf;
            j.is_write = is_write;

            j.pending.store(true, std::memory_order_release);

            remaining -= j.chunk_size;
            host_offset += j.chunk_size;

            if (remaining == 0) {
                break;
            }
        }

        // wake all workers
        cv_.notify_all();

        // wait for completion
        {
            std::unique_lock<std::mutex> lock(mtx_done_);
            cv_done_.wait(
                lock, [&] { return completed_.load(std::memory_order_acquire) == static_cast<int>(n_active_jobs_); });
        }
    }

private:
    size_t nthreads_;
    tt_xy_pair core_;
    uint64_t base_addr_;
    uint64_t size_;
    uint32_t fd_;

    std::vector<std::unique_ptr<TlbWindow>> tlb_windows_;
    std::vector<std::thread> workers_;
    std::vector<Job> jobs_;

    std::atomic<bool> stopping_{false};

    std::mutex mtx_;
    std::condition_variable cv_;

    std::mutex mtx_done_;
    std::condition_variable cv_done_;

    std::atomic<int> completed_{0};
    size_t n_active_jobs_ = 0;

    int numa_node_{0};
};

}  // namespace tt::umd
