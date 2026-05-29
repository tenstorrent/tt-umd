// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

const uint32_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

// ---------------------------------------------------------------------------
// Non-parametrized tests (WH-only / arch-agnostic)
// ---------------------------------------------------------------------------

TEST(ApiSimulationSysmemManager, BasicIOSingleChannel) {
    std::unique_ptr<SimulationSysmemManager> sysmem =
        std::make_unique<SimulationSysmemManager>(1, tt::ARCH::WORMHOLE_B0);

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
    std::unique_ptr<SimulationSysmemManager> sysmem =
        std::make_unique<SimulationSysmemManager>(3, tt::ARCH::WORMHOLE_B0);

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
    std::unique_ptr<SimulationSysmemManager> sysmem =
        std::make_unique<SimulationSysmemManager>(4, tt::ARCH::WORMHOLE_B0);

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
// Mapped buffer tests — parametrized over ARCH (WH and BH).
//
// The mapped-buffer registry keys buffers by their absolute device IO address
// (pcie_base + arena_offset).  Tests use write_mapped_buffer /
// read_mapped_buffer directly, which is the same path taken by
// TTSimTTDevice::pci_dma_{write,read}_bytes after converting the craq-sim
// offset to the absolute key.
// ---------------------------------------------------------------------------

class ApiSimulationSysmemManagerByArch : public ::testing::TestWithParam<tt::ARCH> {};

INSTANTIATE_TEST_SUITE_P(
    Archs,
    ApiSimulationSysmemManagerByArch,
    ::testing::Values(tt::ARCH::WORMHOLE_B0, tt::ARCH::BLACKHOLE),
    [](const ::testing::TestParamInfo<tt::ARCH>& info) {
        switch (info.param) {
            case tt::ARCH::WORMHOLE_B0:
                return "WORMHOLE_B0";
            case tt::ARCH::BLACKHOLE:
                return "BLACKHOLE";
            default:
                return "UNKNOWN";
        }
    });

TEST_P(ApiSimulationSysmemManagerByArch, AllocateSysmemBufferReturnsValidBuffer) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);
    EXPECT_NE(buffer->get_buffer_va(), nullptr);
    EXPECT_GE(buffer->get_buffer_size(), buffer_size);
    // device_io_addr should be non-zero (pcie_base + offset).
    EXPECT_GT(buffer->get_device_io_addr(), 0u);
}

TEST_P(ApiSimulationSysmemManagerByArch, MapExternalBufferCreatesEntry) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    // Allocate a user-managed buffer and map it through the sysmem manager.
    const size_t buffer_size = 8192;
    std::vector<uint8_t> external_buf(buffer_size, 0);
    auto buffer = sysmem->map_sysmem_buffer(external_buf.data(), buffer_size);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->get_buffer_va(), external_buf.data());
    EXPECT_GT(buffer->get_device_io_addr(), 0u);
}

// Verify that write_mapped_buffer / read_mapped_buffer correctly address the
// backing allocation via the registry key (absolute device_io_addr).  This
// exercises the same path as TTSimTTDevice::pci_dma_{write,read}_bytes after
// it adds pcie_base to the craq-sim offset.
TEST_P(ApiSimulationSysmemManagerByArch, WriteReadThroughMappedBuffer) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    const size_t buffer_size = 4096;
    auto buffer = sysmem->allocate_sysmem_buffer(buffer_size);
    ASSERT_NE(buffer, nullptr);

    const uint64_t device_addr = buffer->get_device_io_addr();
    EXPECT_GT(device_addr, 0u);

    // Write a pattern via write_mapped_buffer — this mirrors the
    // pci_dma_write_bytes path (TTSimTTDevice adds pcie_base to the craq-sim
    // offset before calling write_mapped_buffer).
    std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    bool written = sysmem->write_mapped_buffer(device_addr, pattern.data(), pattern.size());
    EXPECT_TRUE(written);

    // Read it back via read_mapped_buffer.
    std::vector<uint8_t> readback(pattern.size(), 0);
    bool read_ok = sysmem->read_mapped_buffer(device_addr, readback.data(), readback.size());
    EXPECT_TRUE(read_ok);
    EXPECT_EQ(pattern, readback);

    // Confirm the data is also visible through the buffer VA.
    EXPECT_EQ(0, std::memcmp(buffer->get_buffer_va(), pattern.data(), pattern.size()));
}

TEST_P(ApiSimulationSysmemManagerByArch, MultipleMappedBuffersAreIndependent) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    auto buf_a = sysmem->allocate_sysmem_buffer(4096);
    auto buf_b = sysmem->allocate_sysmem_buffer(4096);
    ASSERT_NE(buf_a, nullptr);
    ASSERT_NE(buf_b, nullptr);

    // Buffers should have different device IO addresses.
    EXPECT_NE(buf_a->get_device_io_addr(), buf_b->get_device_io_addr());
    // And different virtual addresses.
    EXPECT_NE(buf_a->get_buffer_va(), buf_b->get_buffer_va());
}

TEST_P(ApiSimulationSysmemManagerByArch, DestroyedBufferUnmapsCleanly) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    {
        auto buffer = sysmem->allocate_sysmem_buffer(4096);
        ASSERT_NE(buffer, nullptr);
        // buffer goes out of scope here, triggering unmap callback.
    }

    // Allocating another buffer should succeed (no leaked state).
    auto buffer2 = sysmem->allocate_sysmem_buffer(4096);
    EXPECT_NE(buffer2, nullptr);
}

// Verify that concurrent allocations do not crash (tests the mutex on owned_allocations_).
TEST_P(ApiSimulationSysmemManagerByArch, ConcurrentAllocateDoesNotCrash) {
    auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());

    constexpr int kThreads = 4;
    constexpr size_t kBufSize = 4096;
    std::vector<std::unique_ptr<SysmemBuffer>> results(kThreads);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&sysmem, &results, i]() { results[i] = sysmem->allocate_sysmem_buffer(kBufSize); });
    }
    for (auto& t : threads) {
        t.join();
    }

    // Every allocation should have succeeded with a unique device address.
    for (int i = 0; i < kThreads; ++i) {
        ASSERT_NE(results[i], nullptr) << "Thread " << i << " got null buffer";
        EXPECT_NE(results[i]->get_buffer_va(), nullptr);
    }
    for (int i = 0; i < kThreads; ++i) {
        for (int j = i + 1; j < kThreads; ++j) {
            EXPECT_NE(results[i]->get_device_io_addr(), results[j]->get_device_io_addr())
                << "Buffers from threads " << i << " and " << j << " have same device addr";
        }
    }
}

// Destroy the SimulationSysmemManager while a SysmemBuffer still exists.
// The buffer's unmap callback must not crash (weak_ptr / captured-reference safety).
TEST_P(ApiSimulationSysmemManagerByArch, ManagerDestroyedBeforeBuffer) {
    std::unique_ptr<SysmemBuffer> buffer;
    {
        auto sysmem = std::make_unique<SimulationSysmemManager>(1, GetParam());
        buffer = sysmem->allocate_sysmem_buffer(4096);
        ASSERT_NE(buffer, nullptr);
        // sysmem is destroyed here.
    }
    // buffer goes out of scope — its unmap callback fires after the manager is gone.
    // This must not crash.
    buffer.reset();
    SUCCEED();
}
