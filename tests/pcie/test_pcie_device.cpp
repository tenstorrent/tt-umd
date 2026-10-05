// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstdint>
#include <memory>
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

// A PCIe device with an IOMMU, for pinning scattered pages.
class PcieDevicePinTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto device_ids = PCIDevice::enumerate_devices();
        if (device_ids.empty()) {
            GTEST_SKIP() << "No PCIe devices were enumerated";
        }
        device = std::make_unique<PCIDevice>(device_ids.front());
        if (!device->is_iommu_enabled()) {
            GTEST_SKIP() << "Pinning scattered pages requires an IOMMU";
        }
    }

    std::unique_ptr<PCIDevice> device;
};

// Anonymous memory, unmapped when the test ends however it ends.
struct AnonymousMapping {
    explicit AnonymousMapping(size_t size) :
        size(size), address(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {}

    ~AnonymousMapping() {
        if (address != MAP_FAILED) {
            munmap(address, size);
        }
    }

    AnonymousMapping(const AnonymousMapping&) = delete;
    AnonymousMapping& operator=(const AnonymousMapping&) = delete;

    size_t size = 0;
    void* address = MAP_FAILED;
};

TEST_F(PcieDevicePinTest, ConcurrentPinsOnPinHandles) {
    constexpr size_t num_threads = 4;
    constexpr size_t chunks_per_thread = 16;
    constexpr size_t chunk_size = 1 << 20;
    constexpr size_t total_size = num_threads * chunks_per_thread * chunk_size;
    AnonymousMapping mapping(total_size);
    ASSERT_NE(mapping.address, MAP_FAILED);
    auto* bytes = static_cast<uint8_t*>(mapping.address);

    device->set_pin_handle_count(num_threads);

    // Each thread pins its own interleaved chunks, so the pins run concurrently on separate handles.
    std::vector<uint64_t> iovas(num_threads * chunks_per_thread, 0);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t] {
            for (size_t c = t; c < iovas.size(); c += num_threads) {
                iovas[c] = device->map_for_dma(bytes + c * chunk_size, chunk_size);
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
        EXPECT_NO_THROW(device->unmap_for_dma(bytes + c * chunk_size, chunk_size));
    }

    // The pages were unpinned, so pinning the whole buffer again from this thread must succeed.
    EXPECT_NO_THROW(device->map_for_dma(mapping.address, total_size));
    EXPECT_NO_THROW(device->unmap_for_dma(mapping.address, total_size));
}

TEST_F(PcieDevicePinTest, DuplicatePinThroughAnotherHandleIsRefused) {
    constexpr size_t size = 1 << 20;
    AnonymousMapping mapping(size);
    ASSERT_NE(mapping.address, MAP_FAILED);
    void* buffer = mapping.address;

    device->set_pin_handle_count(2);

    // New threads take consecutive handle slots, so these two pins go through different handles, where the KMD alone
    // would accept both.
    std::thread([&] { EXPECT_NO_THROW(device->map_for_dma(buffer, size)); }).join();
    std::thread([&] { EXPECT_ANY_THROW(device->map_for_dma(buffer, size)); }).join();

    // The refused pin left nothing behind: the range unpins exactly once, and can then be pinned again.
    EXPECT_NO_THROW(device->unmap_for_dma(buffer, size));
    EXPECT_ANY_THROW(device->unmap_for_dma(buffer, size));
    EXPECT_NO_THROW(device->map_for_dma(buffer, size));
    EXPECT_NO_THROW(device->unmap_for_dma(buffer, size));
}
