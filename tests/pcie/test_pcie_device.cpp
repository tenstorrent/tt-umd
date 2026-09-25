// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"

using namespace tt::umd;

TEST(PcieDeviceTest, Numa) {
    std::vector<int> nodes;

    for (auto device_id : PCIDevice::enumerate_devices()) {
        PCIDevice device(device_id);
        nodes.push_back(device.get_numa_node());
    }

    // Acceptable outcomes:
    // 1. all of them are -1 (not a NUMA system)
    // 2. all of them are >= 0 (NUMA system)
    // 3. empty vector (no devices enumerated)

    if (!nodes.empty()) {
        bool all_negative_one = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node == -1; });
        bool all_non_negative = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node >= 0; });

        EXPECT_TRUE(all_negative_one || all_non_negative)
            << "NUMA nodes should either all be -1 (non-NUMA system) or all be non-negative (NUMA system)"
            << " but got: " << fmt::format("{}", fmt::join(nodes, ", "));
    } else {
        SUCCEED() << "No PCIe devices were enumerated";
    }
}

TEST(PcieDeviceTest, ConcurrentPinsOnPinHandles) {
    const auto device_ids = PCIDevice::enumerate_devices();
    if (device_ids.empty()) {
        GTEST_SKIP() << "No PCIe devices were enumerated";
    }
    PCIDevice device(device_ids.front());
    if (!device.is_iommu_enabled()) {
        GTEST_SKIP() << "Pinning scattered pages requires an IOMMU";
    }

    constexpr size_t num_threads = 4;
    constexpr size_t chunks_per_thread = 16;
    const size_t chunk_size = 1 << 20;
    const size_t total_size = num_threads * chunks_per_thread * chunk_size;
    void* buffer = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(buffer, MAP_FAILED);
    auto* bytes = static_cast<uint8_t*>(buffer);

    device.set_pin_handle_count(num_threads);

    // Each thread pins its own interleaved chunks, so the pins run concurrently on separate handles.
    std::vector<uint64_t> iovas(num_threads * chunks_per_thread, 0);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t] {
            for (size_t c = t; c < iovas.size(); c += num_threads) {
                iovas[c] = device.map_for_dma(bytes + c * chunk_size, chunk_size);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (uint64_t iova : iovas) {
        EXPECT_NE(iova, 0u);
    }

    // Unpin every chunk from this thread, which pinned none of them; each must go back through its pinning handle.
    for (size_t c = 0; c < iovas.size(); c++) {
        EXPECT_NO_THROW(device.unmap_for_dma(bytes + c * chunk_size, chunk_size));
    }

    // The pages were unpinned, so pinning the whole buffer again from this thread must succeed.
    EXPECT_NO_THROW(device.map_for_dma(buffer, total_size));
    EXPECT_NO_THROW(device.unmap_for_dma(buffer, total_size));
    munmap(buffer, total_size);
}
