// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tt-umd/chip/chip.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/cluster.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/dma.hpp"
#include "utils.hpp"

using namespace tt::umd;

namespace {

constexpr size_t TRANSFER_SIZE = 64 * 1024;

// Polls the way the owner of a transfer does: until it reports the transfer done. Returns IN_PROGRESS on timeout.
template <typename Check>
DmaTransferState wait_for_transfer(Check check) {
    static constexpr auto TIMEOUT = std::chrono::milliseconds(5'000);
    static constexpr auto BUSY_POLL_WINDOW = std::chrono::microseconds(0);
    static constexpr auto POLL_INTERVAL = std::chrono::microseconds(100);
    DmaTransferState state = DmaTransferState::IN_PROGRESS;
    utils::poll_until(
        [&] {
            state = check();
            return state != DmaTransferState::IN_PROGRESS;
        },
        TIMEOUT,
        BUSY_POLL_WINDOW,
        POLL_INTERVAL);
    return state;
}

DmaTransferState wait_for_write(TTDevice* tt_device) {
    return wait_for_transfer([&] { return tt_device->dma_write_zero_copy_check(); });
}

DmaTransferState wait_for_read(TTDevice* tt_device) {
    return wait_for_transfer([&] { return tt_device->dma_read_zero_copy_check(); });
}

}  // namespace

class DmaZeroCopyAsync : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No Tenstorrent PCI devices found.";
        }
        if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
            GTEST_SKIP() << "These tests only cover zero-copy DMA through the IOMMU.";
        }

        cluster_ = test_utils::make_default_test_cluster();
        const ChipId chip = *cluster_->get_target_mmio_device_ids().begin();
        tt_device_ = cluster_->get_chip(chip)->get_tt_device();
        dram_cores_ = cluster_->get_soc_descriptor(chip).get_cores(CoreType::DRAM);
        buffer_ = cluster_->get_chip(chip)->get_sysmem_manager()->allocate_sysmem_buffer(2 * TRANSFER_SIZE);
        host_ = static_cast<uint8_t*>(buffer_->get_va());
    }

    std::unique_ptr<Cluster> cluster_;
    TTDevice* tt_device_ = nullptr;
    std::vector<CoreCoord> dram_cores_;
    std::unique_ptr<SysmemBuffer> buffer_;
    uint8_t* host_ = nullptr;
};

TEST_F(DmaZeroCopyAsync, WriteLandsOnDevice) {
    test_utils::fill_with_random_bytes(host_, TRANSFER_SIZE);

    ASSERT_TRUE(tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));
    EXPECT_EQ(wait_for_write(tt_device_), DmaTransferState::FINISHED);
    EXPECT_EQ(tt_device_->dma_write_zero_copy_check(), DmaTransferState::IDLE);

    std::vector<uint8_t> readback(TRANSFER_SIZE);
    tt_device_->read_from_device(readback.data(), dram_cores_[0], 0x0, TRANSFER_SIZE);
    EXPECT_EQ(std::memcmp(readback.data(), host_, TRANSFER_SIZE), 0);
}

// Rejected before anything reaches the device, so the source buffer does not have to cover the size.
TEST_F(DmaZeroCopyAsync, WriteLargerThanMaxSizeThrows) {
    const size_t size = tt_device_->dma_zero_copy_max_size(0x0) + 4;

    EXPECT_THROW(
        (void)tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, size, dram_cores_[0]), std::exception);
    EXPECT_EQ(tt_device_->dma_write_zero_copy_check(), DmaTransferState::IDLE);
}

// Each chunk runs right up to dma_zero_copy_max_size(), so the readback catches a max size larger than what actually
// lands where it should. Starts just below 16 MiB so that current hardware splits it.
TEST_F(DmaZeroCopyAsync, WriteSplitByMaxSizeLandsOnDevice) {
    constexpr uint64_t DEVICE_ADDR = 16 * 1024 * 1024 - TRANSFER_SIZE / 2;
    test_utils::fill_with_random_bytes(host_, TRANSFER_SIZE);

    for (size_t offset = 0; offset < TRANSFER_SIZE;) {
        const size_t chunk = std::min(tt_device_->dma_zero_copy_max_size(DEVICE_ADDR + offset), TRANSFER_SIZE - offset);
        ASSERT_TRUE(tt_device_->dma_write_zero_copy_start(
            buffer_->get_iova(offset), DEVICE_ADDR + offset, chunk, dram_cores_[0]));
        ASSERT_EQ(wait_for_write(tt_device_), DmaTransferState::FINISHED);
        offset += chunk;
    }

    std::vector<uint8_t> readback(TRANSFER_SIZE);
    tt_device_->read_from_device(readback.data(), dram_cores_[0], DEVICE_ADDR, TRANSFER_SIZE);
    EXPECT_EQ(std::memcmp(readback.data(), host_, TRANSFER_SIZE), 0);
}

TEST_F(DmaZeroCopyAsync, StartWhileTransferInFlightReturnsFalse) {
    ASSERT_TRUE(tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));

    EXPECT_FALSE(
        tt_device_->dma_write_zero_copy_start(buffer_->get_iova(TRANSFER_SIZE), 0x0, TRANSFER_SIZE, dram_cores_[1]));

    EXPECT_EQ(wait_for_write(tt_device_), DmaTransferState::FINISHED);
}

// Another thread sees the transfer in flight but can neither start its own nor take the finish away from the owner.
TEST_F(DmaZeroCopyAsync, OnlyTheStartingThreadSeesItsTransferFinish) {
    ASSERT_TRUE(tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));

    std::thread([&] {
        EXPECT_FALSE(tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));
        EXPECT_EQ(tt_device_->dma_write_zero_copy_check(), DmaTransferState::IN_PROGRESS);
    }).join();

    EXPECT_EQ(wait_for_write(tt_device_), DmaTransferState::FINISHED);
    std::thread([&] { EXPECT_EQ(tt_device_->dma_write_zero_copy_check(), DmaTransferState::IDLE); }).join();
}

TEST_F(DmaZeroCopyAsync, BlockingDmaFromTheOwningThreadThrows) {
    ASSERT_TRUE(tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));

    EXPECT_THROW(
        tt_device_->dma_write_zero_copy(buffer_->get_iova(TRANSFER_SIZE), 0x0, TRANSFER_SIZE, dram_cores_[1]),
        std::exception);

    EXPECT_EQ(wait_for_write(tt_device_), DmaTransferState::FINISHED);
}

// The device stays busy until the owner sees its transfer finish, so a blocking DMA from another thread waits for it.
TEST_F(DmaZeroCopyAsync, BlockingDmaFromAnotherThreadWaitsForTheOwner) {
    ASSERT_TRUE(tt_device_->dma_write_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));

    std::atomic<bool> blocking_done = false;
    std::thread blocking([&] {
        tt_device_->dma_write_zero_copy(buffer_->get_iova(TRANSFER_SIZE), 0x0, TRANSFER_SIZE, dram_cores_[1]);
        blocking_done = true;
    });
    // Gives the blocking call time to reach its wait; the hardware transfer is long done by now.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(blocking_done);

    EXPECT_EQ(wait_for_write(tt_device_), DmaTransferState::FINISHED);
    blocking.join();
    EXPECT_TRUE(blocking_done);
}

TEST_F(DmaZeroCopyAsync, ReadLandsInHostMemory) {
    if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Device-to-host DMA is not supported on Blackhole.";
    }
    std::vector<uint8_t> pattern(TRANSFER_SIZE);
    test_utils::fill_with_random_bytes(pattern.data(), TRANSFER_SIZE);
    tt_device_->write_to_device(pattern.data(), dram_cores_[0], 0x0, TRANSFER_SIZE);
    std::memset(host_, 0, TRANSFER_SIZE);

    ASSERT_TRUE(tt_device_->dma_read_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]));
    EXPECT_EQ(wait_for_read(tt_device_), DmaTransferState::FINISHED);

    EXPECT_EQ(std::memcmp(host_, pattern.data(), TRANSFER_SIZE), 0);
}

TEST_F(DmaZeroCopyAsync, ReadOnBlackholeThrows) {
    if (tt_device_->get_arch() != tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Device-to-host DMA is supported on this architecture.";
    }

    EXPECT_THROW(
        (void)tt_device_->dma_read_zero_copy_start(buffer_->get_iova(0), 0x0, TRANSFER_SIZE, dram_cores_[0]),
        std::exception);
    EXPECT_EQ(tt_device_->dma_read_zero_copy_check(), DmaTransferState::IDLE);
}
