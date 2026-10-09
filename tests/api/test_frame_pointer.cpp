// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(__x86_64__)
#include <asm/perf_regs.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tt-umd/cluster.hpp"

namespace tt::umd {

#if defined(__x86_64__)

// Records the user-space rbp register at a fixed interval of CPU time. The kernel only copies the register value into
// each sample, so sampling never dereferences rbp and cannot reach the device.
class RbpSampler {
public:
    ~RbpSampler() {
        if (ring_ != MAP_FAILED) {
            munmap(ring_, page_size_ + data_size_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // False when perf_event_open is not permitted, which is the one failure a caller can reasonably skip over.
    bool start() {
        perf_event_attr attr{.type = PERF_TYPE_SOFTWARE, .size = sizeof(attr), .config = PERF_COUNT_SW_CPU_CLOCK};
        attr.sample_period = 100'000;  // ns of CPU time.
        attr.sample_type = PERF_SAMPLE_REGS_USER;
        attr.sample_regs_user = 1ULL << PERF_REG_X86_BP;
        attr.exclude_kernel = 1;
        fd_ = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC));
        if (fd_ < 0) {
            return false;
        }
        // A writable mapping puts the ring in non-overwrite mode: once it is full the kernel drops new samples rather
        // than running over ones drain() has not read yet.
        ring_ = static_cast<perf_event_mmap_page*>(
            mmap(nullptr, page_size_ + data_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (ring_ == MAP_FAILED) {
            throw std::runtime_error("Could not map the perf ring buffer.");
        }
        return true;
    }

    // Stops sampling, so that a final drain() sees a ring nothing is writing to any more.
    void stop() const { ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0); }

    // Appends the samples taken since the last call and frees the ring space they occupied. Call it often enough that
    // the ring cannot fill up, otherwise the kernel drops everything after it does.
    void drain(std::vector<uint64_t>& rbp_samples) const {
        const uint64_t head = __atomic_load_n(&ring_->data_head, __ATOMIC_ACQUIRE);
        uint64_t offset = ring_->data_tail;
        while (offset + sizeof(perf_event_header) <= head) {
            perf_event_header header;
            copy_out(offset, &header, sizeof(header));
            if (header.size < sizeof(header) || offset + header.size > head) {
                break;  // Truncated or malformed, so the rest of the ring cannot be parsed either.
            }
            if (header.type == PERF_RECORD_SAMPLE && header.size == sizeof(header) + 2 * sizeof(uint64_t)) {
                uint64_t sample[2];  // abi, rbp
                copy_out(offset + sizeof(header), sample, sizeof(sample));
                rbp_samples.push_back(sample[1]);
            }
            offset += header.size;
        }
        __atomic_store_n(&ring_->data_tail, offset, __ATOMIC_RELEASE);
    }

private:
    // data_head and data_tail are free-running byte counters into a ring, so a record can straddle the end of the
    // buffer and has to be copied out before it can be read.
    void copy_out(uint64_t from, void* dest, size_t size) const {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(ring_) + page_size_;
        const size_t start = from % data_size_;
        const size_t until_end = std::min(size, data_size_ - start);
        std::memcpy(dest, data + start, until_end);
        std::memcpy(static_cast<uint8_t*>(dest) + until_end, data, size - until_end);
    }

    const size_t page_size_ = sysconf(_SC_PAGESIZE);
    const size_t data_size_ = 64 * page_size_;
    int fd_ = -1;
    perf_event_mmap_page* ring_ = static_cast<perf_event_mmap_page*>(MAP_FAILED);
};

// The address ranges the device's BARs are mapped at.
std::vector<std::pair<uint64_t, uint64_t>> bar_mappings() {
    std::vector<std::pair<uint64_t, uint64_t>> mappings;
    std::ifstream maps("/proc/self/maps");
    for (std::string line; std::getline(maps, line);) {
        if (line.find("/dev/tenstorrent") != std::string::npos) {
            const size_t dash = line.find('-');
            mappings.emplace_back(std::stoull(line, nullptr, 16), std::stoull(line.substr(dash + 1), nullptr, 16));
        }
    }
    return mappings;
}

#endif

// Profilers that record call chains (Tracy, perf -g) have the kernel walk frame pointers from the sampled rbp, reading
// memory at rbp. If rbp holds a pointer into a BAR mapping, that read goes to the device and can wedge it
// (tt-umd#3594). Sample rbp while UMD does register I/O, and check it never points into device memory.
//
// A regression only shows up on a build that inlines TlbWindow::validate into the register I/O path, which is what left
// the window pointer in rbp. That is the unity build; #3390 turns it on for UMD.
TEST(FramePointer, RbpNeverPointsIntoDeviceMemory) {
#if !defined(__x86_64__)
    GTEST_SKIP() << "Samples the x86-64 frame pointer register.";
#else
    RbpSampler sampler;
    if (!sampler.start()) {
        GTEST_SKIP() << "perf_event_open not permitted.";
    }

    Cluster cluster;
    cluster.start_device({});

    const std::vector<std::pair<uint64_t, uint64_t>> device_memory = bar_mappings();
    ASSERT_FALSE(device_memory.empty())
        << "No BAR mapping to compare against, so the check below would pass vacuously.";

    std::vector<uint64_t> rbp_samples;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < end) {
        for (const CoreCoord& core : cluster.get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
            cluster.assert_risc_reset(0, core, RiscType::ALL);  // Register read-modify-write through a TLB window.
        }
        sampler.drain(rbp_samples);
    }
    sampler.stop();
    sampler.drain(rbp_samples);

    ASSERT_FALSE(rbp_samples.empty());
    size_t hits = 0;
    for (const uint64_t rbp : rbp_samples) {
        for (const auto& [lo, hi] : device_memory) {
            hits += rbp >= lo && rbp < hi;
        }
    }
    EXPECT_EQ(hits, 0u) << hits << " of " << rbp_samples.size() << " samples caught rbp pointing into a BAR mapping.";
#endif
}

}  // namespace tt::umd
