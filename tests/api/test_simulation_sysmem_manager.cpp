// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/mman.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

const uint32_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

TEST(ApiSimulationSysmemManager, BasicIOSingleChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem = std::make_unique<SimulationSysmemManager>(1);

    const HugepageMapping channel_0 = sysmem->get_hugepage_mapping(0);

    EXPECT_EQ(channel_0.mapping_size, HUGEPAGE_REGION_SIZE);

    void* channel_0_mapping = channel_0.mapping;

    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(0, data_write.data(), 0, data_write.size());

    std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
    sysmem->read_from_sysmem(0, data_read.data(), 0, data_read.size());

    EXPECT_EQ(data_write, data_read);

    for (int i = 0; i < data_write.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t*>(channel_0_mapping)[i], data_write[i]);
    }
}

TEST(ApiSimulationSysmemManager, BasicIOMultiChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem = std::make_unique<SimulationSysmemManager>(3);

    for (int i = 0; i < 3; i++) {
        const HugepageMapping channel = sysmem->get_hugepage_mapping(i);

        EXPECT_EQ(channel.mapping_size, HUGEPAGE_REGION_SIZE);

        void* channel_mapping = channel.mapping;

        std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

        sysmem->write_to_sysmem(i, data_write.data(), 0, data_write.size());

        std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
        sysmem->read_from_sysmem(i, data_read.data(), 0, data_read.size());

        EXPECT_EQ(data_write, data_read);

        for (int j = 0; j < data_write.size(); j++) {
            EXPECT_EQ(static_cast<uint8_t*>(channel_mapping)[j], data_write[j]);
        }
    }
}

TEST(ApiSimulationSysmemManager, TestFourChannels) {
    std::unique_ptr<SimulationSysmemManager> sysmem = std::make_unique<SimulationSysmemManager>(4);

    const HugepageMapping channel_3 = sysmem->get_hugepage_mapping(3);

    EXPECT_EQ(channel_3.mapping_size, HUGEPAGE_CHANNEL_3_SIZE_LIMIT);

    void* channel_3_mapping = channel_3.mapping;

    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(3, data_write.data(), 0, data_write.size());

    std::vector<uint8_t> data_read = std::vector<uint8_t>(data_write.size(), 0);
    sysmem->read_from_sysmem(3, data_read.data(), 0, data_read.size());

    EXPECT_EQ(data_write, data_read);

    for (int i = 0; i < data_write.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t*>(channel_3_mapping)[i], data_write[i]);
    }
}

// ---------------------------------------------------------------------------
// SysmemBuffer-related tests for SimulationSysmemManager.
// ---------------------------------------------------------------------------

TEST(ApiSimulationSysmemManager, AllocateSysmemBufferReturnsBuffer) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(0, tt::ARCH::WORMHOLE_B0);
    const size_t size = 1 << 20;

    auto buffer = sysmem->allocate_sysmem_buffer(size, /*map_to_noc=*/true);
    ASSERT_NE(buffer, nullptr);
    EXPECT_NE(buffer->get_buffer_va(), nullptr);
    EXPECT_EQ(buffer->get_buffer_size(), size);
    ASSERT_TRUE(buffer->get_noc_addr().has_value());
}

TEST(ApiSimulationSysmemManager, MapSysmemBufferReturnsBuffer) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(0, tt::ARCH::WORMHOLE_B0);
    const size_t size = 1 << 20;

    std::vector<uint8_t> backing(size, 0);
    auto buffer = sysmem->map_sysmem_buffer(backing.data(), size, /*map_to_noc=*/true);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->get_buffer_va(), backing.data());
    EXPECT_EQ(buffer->get_buffer_size(), size);
    ASSERT_TRUE(buffer->get_noc_addr().has_value());
}

TEST(ApiSimulationSysmemManager, NocAddrAssignedAboveChannelRegion) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(0, tt::ARCH::WORMHOLE_B0);
    const uint64_t pcie_base = sysmem->get_pcie_base();
    const size_t size = 1 << 20;

    auto first = sysmem->allocate_sysmem_buffer(size, /*map_to_noc=*/true);
    auto second = sysmem->allocate_sysmem_buffer(size, /*map_to_noc=*/true);

    ASSERT_TRUE(first->get_noc_addr().has_value());
    ASSERT_TRUE(second->get_noc_addr().has_value());

    // First buffer sits at the start of the mapped-buffer region (4 GiB above pcie_base).
    EXPECT_EQ(first->get_noc_addr().value(), pcie_base + (4ULL << 30));
    // Second buffer follows immediately after.
    EXPECT_EQ(second->get_noc_addr().value(), first->get_noc_addr().value() + size);
}

TEST(ApiSimulationSysmemManager, MapToNocFalseHidesNocAddr) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(0, tt::ARCH::WORMHOLE_B0);
    const size_t size = 1 << 20;

    auto buffer = sysmem->allocate_sysmem_buffer(size, /*map_to_noc=*/false);
    ASSERT_NE(buffer, nullptr);
    EXPECT_FALSE(buffer->get_noc_addr().has_value());
}

TEST(ApiSimulationSysmemManager, RoutingHostInitWriteAndRead) {
    // Use 1 channel so we can verify routing covers channel regions too.
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, tt::ARCH::WORMHOLE_B0);

    // Write into channel 0 by paddr (channel 0 is at paddr 0).
    std::vector<uint8_t> chan_pattern = {10, 20, 30, 40};
    uint8_t* chan_va = sysmem->find_paddr_host_va(0x100, chan_pattern.size());
    ASSERT_NE(chan_va, nullptr);
    std::memcpy(chan_va, chan_pattern.data(), chan_pattern.size());
    // Confirm same bytes show up via the channel-keyed sysmem accessor.
    std::vector<uint8_t> chan_readback(chan_pattern.size(), 0);
    sysmem->read_from_sysmem(0, chan_readback.data(), 0x100, chan_readback.size());
    EXPECT_EQ(chan_readback, chan_pattern);

    // Allocate a buffer and exercise paddr routing into it.
    const size_t size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(size, /*map_to_noc=*/true);
    const uint64_t pcie_base = sysmem->get_pcie_base();
    const uint64_t buf_paddr = buffer->get_noc_addr().value() - pcie_base;

    std::vector<uint8_t> buf_pattern(size);
    for (size_t i = 0; i < size; ++i) {
        buf_pattern[i] = static_cast<uint8_t>(i % 256);
    }

    uint8_t* buf_routed = sysmem->find_paddr_host_va(buf_paddr, size);
    ASSERT_NE(buf_routed, nullptr);
    EXPECT_EQ(buf_routed, buffer->get_buffer_va());
    std::memcpy(buf_routed, buf_pattern.data(), size);

    // Read the same data back through the user-facing buffer pointer.
    EXPECT_EQ(std::memcmp(buffer->get_buffer_va(), buf_pattern.data(), size), 0);
}

TEST(ApiSimulationSysmemManager, BufferDeregistersOnDestroy) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(0, tt::ARCH::WORMHOLE_B0);
    const size_t size = 4096;

    uint64_t paddr_to_check;
    {
        auto buffer = sysmem->allocate_sysmem_buffer(size, /*map_to_noc=*/true);
        paddr_to_check = buffer->get_noc_addr().value() - sysmem->get_pcie_base();
        EXPECT_NE(sysmem->find_paddr_host_va(paddr_to_check, size), nullptr);
    }
    // After the buffer is destroyed, the region must no longer be routable.
    EXPECT_EQ(sysmem->find_paddr_host_va(paddr_to_check, size), nullptr);
}

TEST(ApiSimulationSysmemManager, FindPaddrRejectsOutOfRange) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, tt::ARCH::WORMHOLE_B0);
    // 1 channel covers paddr [0, 1GiB). Above that there are no buffers.
    EXPECT_EQ(sysmem->find_paddr_host_va(2ULL << 30, 4), nullptr);

    // A request that straddles the end of channel 0 must also fail.
    EXPECT_EQ(sysmem->find_paddr_host_va((1ULL << 30) - 2, 8), nullptr);
}
