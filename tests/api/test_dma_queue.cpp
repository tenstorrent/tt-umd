// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#ifdef TT_UMD_HAS_DMA_QUEUE

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/pcie/dma_queue.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"

using namespace tt::umd;

namespace {

constexpr size_t TRANSFER_SIZE = 64 * 1024;
constexpr uint64_t WINDOW_2M = 2 * 1024 * 1024;
constexpr uint64_t DEVICE_ADDR = 16 * 1024 * 1024;

}  // namespace

class DmaQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent PCI devices found.";
        }
        if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
            GTEST_SKIP() << "These tests pin their source buffer through the IOMMU.";
        }

        cluster_ = test_utils::make_default_test_cluster();
        const ChipId chip = *cluster_->get_target_mmio_device_ids().begin();
        tt_device_ = cluster_->get_chip(chip)->get_tt_device();
        if (tt_device_->get_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "The driver queues DMA writes on Blackhole only.";
        }

        const SocDescriptor& soc = cluster_->get_soc_descriptor(chip);
        for (int bank = 0; bank < soc.get_num_dram_channels(); bank++) {
            dram_cores_.push_back(soc.get_dram_core_for_channel(bank, 0));
            endpoints_.push_back(soc.translate_coord_to(dram_cores_.back(), CoordSystem::TRANSLATED));
        }
        buffer_ = cluster_->get_chip(chip)->get_sysmem_manager()->allocate_sysmem_buffer(TRANSFER_SIZE);
        host_ = static_cast<uint8_t*>(buffer_->get_va());
        test_utils::fill_with_random_bytes(host_, TRANSFER_SIZE);
    }

    void expect_landed(size_t bank, uint64_t addr) {
        std::vector<uint8_t> readback(TRANSFER_SIZE);
        tt_device_->read_from_device(readback.data(), dram_cores_[bank], addr, TRANSFER_SIZE);
        EXPECT_EQ(std::memcmp(readback.data(), host_, TRANSFER_SIZE), 0);
    }

    DmaWrite whole_buffer_to(size_t bank, uint64_t addr) {
        return {buffer_->get_iova(0), endpoints_[bank], addr, TRANSFER_SIZE};
    }

    std::unique_ptr<Cluster> cluster_;
    TTDevice* tt_device_ = nullptr;
    // One core of each DRAM bank, and the same cores as the queue addresses them.
    std::vector<CoreCoord> dram_cores_;
    std::vector<tt::xy_pair> endpoints_;
    std::unique_ptr<SysmemBuffer> buffer_;
    uint8_t* host_ = nullptr;
};

TEST_F(DmaQueueTest, BatchLandsOnEveryEndpoint) {
    DmaQueue queue(*tt_device_->get_pci_device());

    const uint64_t ticket = queue.submit({whole_buffer_to(0, DEVICE_ADDR), whole_buffer_to(1, DEVICE_ADDR)});
    queue.wait(ticket);

    EXPECT_EQ(ticket, 1);
    EXPECT_EQ(queue.completed(), 1);
    expect_landed(0, DEVICE_ADDR);
    expect_landed(1, DEVICE_ADDR);
}

TEST_F(DmaQueueTest, WriteCrossingWindowBoundaryLands) {
    DmaQueue queue(*tt_device_->get_pci_device());
    const uint64_t addr = DEVICE_ADDR + WINDOW_2M - TRANSFER_SIZE / 2;

    queue.wait(queue.submit({whole_buffer_to(0, addr)}));

    expect_landed(0, addr);
}

// Three blocks of one endpoint's address space through a single window.
TEST_F(DmaQueueTest, BatchWithMoreRegionsThanWindowsLands) {
    DmaQueue queue(*tt_device_->get_pci_device(), DmaTraffic{1, WINDOW_2M});
    ASSERT_EQ(queue.window_count(), 1);

    queue.wait(queue.submit({
        whole_buffer_to(0, DEVICE_ADDR),
        whole_buffer_to(0, DEVICE_ADDR + WINDOW_2M),
        whole_buffer_to(0, DEVICE_ADDR + 2 * WINDOW_2M),
    }));

    expect_landed(0, DEVICE_ADDR);
    expect_landed(0, DEVICE_ADDR + WINDOW_2M);
    expect_landed(0, DEVICE_ADDR + 2 * WINDOW_2M);
}

TEST_F(DmaQueueTest, WideRangeGetsLargeWindows) {
    constexpr uint64_t ONE_GIB = 1024 * 1024 * 1024;
    DmaQueue queue(*tt_device_->get_pci_device(), DmaTraffic{1, ONE_GIB});
    EXPECT_EQ(queue.window_size(), 4 * ONE_GIB);

    queue.wait(queue.submit({whole_buffer_to(0, DEVICE_ADDR), whole_buffer_to(0, ONE_GIB / 2)}));

    expect_landed(0, DEVICE_ADDR);
    expect_landed(0, ONE_GIB / 2);
}

TEST_F(DmaQueueTest, BatchesCompleteInOrder) {
    DmaQueue queue(*tt_device_->get_pci_device());

    const uint64_t first = queue.submit({whole_buffer_to(0, DEVICE_ADDR)});
    const uint64_t second = queue.submit({whole_buffer_to(1, DEVICE_ADDR)});
    queue.wait(second);

    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
    EXPECT_EQ(queue.completed(), 2);
}

TEST_F(DmaQueueTest, SourceOutsidePinnedBufferThrows) {
    DmaQueue queue(*tt_device_->get_pci_device());

    const uint64_t ticket = queue.submit({{buffer_->get_iova(0) + TRANSFER_SIZE, endpoints_[0], DEVICE_ADDR, 4096}});

    EXPECT_THROW(queue.wait(ticket), std::exception);
}

#endif  // TT_UMD_HAS_DMA_QUEUE
