// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/process_utils.hpp"
#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/chip/local_chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/chip_helpers/silicon_sysmem_manager.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/kmd_versions.hpp"

using namespace tt;
using namespace tt::umd;

constexpr uint32_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

TEST(ApiSysmemManager, BasicIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        // A chip with one host memory channel, mapped when the chip starts.
        std::unique_ptr<LocalChip> chip = LocalChip::create(create_tt_device(pci_device_id), 1);
        chip->start_device();

        // Simple write and read test.
        std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        chip->write_to_sysmem(0, data_write.data(), 0, data_write.size() * sizeof(uint32_t));

        std::vector<uint32_t> data_read = std::vector<uint32_t>(data_write.size(), 0);
        chip->read_from_sysmem(0, data_read.data(), 0, data_read.size() * sizeof(uint32_t));

        EXPECT_EQ(data_write, data_read);

        // Channel 1 is not setup, so this should throw an exception.
        EXPECT_THROW(
            chip->write_to_sysmem(1, data_write.data(), 0, data_write.size() * sizeof(uint32_t)), std::runtime_error);

        // When we write over the limit, the address is wrapped around the hugepage size.
        chip->write_to_sysmem(0, data_write.data(), HUGEPAGE_REGION_SIZE + 0x100, data_write.size() * sizeof(uint32_t));
        data_read = std::vector<uint32_t>(data_write.size(), 0);
        chip->read_from_sysmem(0, data_read.data(), 0x100, data_read.size() * sizeof(uint32_t));
        EXPECT_EQ(data_write, data_read);

        chip->close_device();
    }
}

TEST(ApiSysmemManager, SysmemBuffers) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    std::unique_ptr<PCIDevice> pci_device = std::make_unique<PCIDevice>(pci_device_ids[0]);

    if (!pci_device->is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    if (pci_device->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole, as PCIE DMA is not supported on Blackhole.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    constexpr uint32_t one_mb = 1 << 20;
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(2 * one_mb);

    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];
    const xy_pair tensix_core_xy =
        cluster->get_soc_descriptor(mmio_chip).translate_chip_coord_to_translated(tensix_core, get_selected_noc_id());

    // Zero out 1MB of Tensix L1.
    std::vector<uint8_t> data_write(one_mb, 0);
    cluster->write_to_device(data_write.data(), one_mb, mmio_chip, tensix_core, 0);

    uint8_t* sysmem_data = static_cast<uint8_t*>(sysmem_buffer->get_va());

    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data[i] = static_cast<uint8_t>(i % 256);
    }

    // Write pattern to first 1MB of Tensix L1.
    sysmem_buffer->dma_write_to_device(0, one_mb, tensix_core_xy, 0);

    // Read regularly to check Tensix L1 matches the pattern.
    std::vector<uint8_t> readback(one_mb, 0);
    cluster->dma_read_from_device(readback.data(), one_mb, mmio_chip, tensix_core, 0);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], readback[i]);
    }

    uint8_t* sysmem_data_readback = sysmem_data + one_mb;

    // Zero out sysmem_data_readback. We are doing this in case the pattern was already present in the sysmem buffer.
    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data_readback[i] = 0;
    }

    // Read data back from Tensix L1 to sysmem_data_readback.
    sysmem_buffer->dma_read_from_device(one_mb, one_mb, tensix_core_xy, 0);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], sysmem_data_readback[i]);
    }
}

TEST(ApiSysmemManager, SysmemBufferUnaligned) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    std::unique_ptr<PCIDevice> pci_device = std::make_unique<PCIDevice>(pci_device_ids[0]);
    if (!pci_device->is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    if (pci_device->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole, as PCIE DMA is not supported on Blackhole.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    constexpr uint32_t one_mb = 1 << 20;
    void* mapping =
        mmap(nullptr, 2 * one_mb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    // It's important that this offset is not a multiple of the page size.
    constexpr size_t unaligned_offset = 100;
    void* mapping_buffer = static_cast<uint8_t*>(mapping) + unaligned_offset;  // Offset by 1MB

    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping_buffer, one_mb);

    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];
    const xy_pair tensix_core_xy =
        cluster->get_soc_descriptor(mmio_chip).translate_chip_coord_to_translated(tensix_core, get_selected_noc_id());

    // Zero out 1MB of Tensix L1.
    std::vector<uint8_t> data_write(one_mb, 0);
    cluster->write_to_device(data_write.data(), one_mb, mmio_chip, tensix_core, 0);

    uint8_t* sysmem_data = static_cast<uint8_t*>(sysmem_buffer->get_va());

    EXPECT_EQ(sysmem_data, mapping_buffer);
    EXPECT_EQ(sysmem_buffer->get_size(), one_mb);

    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data[i] = static_cast<uint8_t>(i % 256);
    }

    // Write pattern to first 1MB of Tensix L1.
    sysmem_buffer->dma_write_to_device(0, one_mb, tensix_core_xy, 0);

    // Read regularly to check Tensix L1 matches the pattern.
    std::vector<uint8_t> readback(one_mb, 0);
    cluster->dma_read_from_device(readback.data(), one_mb, mmio_chip, tensix_core, 0);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], readback[i]);
    }

    // Zero out sysmem_data before reading back.
    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data[i] = 0;
    }

    // Read data back from Tensix L1 to sysmem_data.
    sysmem_buffer->dma_read_from_device(0, one_mb, tensix_core_xy, 0);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], readback[i]);
    }
}

TEST(ApiSysmemManager, SysmemBufferFunctions) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    constexpr size_t mmap_size = 20;
    constexpr size_t buf_size = 10;

    // Size is not multiple of page size.
    void* mapping = mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    void* mapped_buffer = static_cast<uint8_t*>(mapping) + buf_size;  // Offset by 10 bytes

    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapped_buffer, buf_size);

    EXPECT_EQ(sysmem_buffer->get_size(), buf_size);
    EXPECT_EQ(sysmem_buffer->get_va(), mapped_buffer);
}

// allocate_sysmem_buffer() mmaps the backing memory, so the buffer has to free it on destruction.
// Before the buffer owned its memory, every allocation leaked the whole mapping.
TEST(ApiSysmemManager, AllocatedBufferFreesBackingMemory) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    constexpr size_t buf_size = 64ULL << 20;
    const size_t buf_size_kib = buf_size >> 10;
    constexpr int iterations = 8;

    // Warm up so one-time allocations do not land inside the measurement.
    { std::unique_ptr<SysmemBuffer> warmup = sysmem_manager->allocate_sysmem_buffer(buf_size); }

    const size_t rss_before = test_utils::read_rss_kib();
    if (rss_before == 0) {
        GTEST_SKIP() << "Could not read VmRSS from /proc/self/status.";
    }

    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    for (int i = 0; i < iterations; i++) {
        std::unique_ptr<SysmemBuffer> buffer = sysmem_manager->allocate_sysmem_buffer(buf_size);
        ASSERT_NE(buffer, nullptr);
        // Touch one byte per page so the whole mapping is resident. Touching only the first page would
        // leave a leak invisible: RSS would grow by a page per iteration rather than by the buffer size.
        uint8_t* bytes = static_cast<uint8_t*>(buffer->get_va());
        for (size_t offset = 0; offset < buf_size; offset += page_size) {
            bytes[offset] = static_cast<uint8_t>(i);
        }
    }

    const size_t rss_after = test_utils::read_rss_kib();
    const size_t growth_kib = rss_after > rss_before ? rss_after - rss_before : 0;

    // Leaking would retain every iteration's mapping. Allow one buffer of slack for allocator noise.
    EXPECT_LT(growth_kib, buf_size_kib) << "RSS grew by " << growth_kib << " KiB across " << iterations
                                        << " allocate/destroy cycles of " << buf_size_kib << " KiB each";
}

// The manager pins for exactly one device and stamps that device's id into every buffer, so the id
// on a buffer must match the TTDevice it was pinned against.
TEST(ApiSysmemManager, SysmemBufferCommunicationId) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    for (const ChipId mmio_chip : cluster->get_target_mmio_device_ids()) {
        Chip* chip = cluster->get_chip(mmio_chip);
        SysmemManager* sysmem_manager = chip->get_sysmem_manager();
        const int expected_id = chip->get_tt_device()->get_communication_device_id();

        EXPECT_EQ(sysmem_manager->get_communication_id(), expected_id);

        constexpr size_t buf_size = 1 << 20;
        std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(buf_size);
        ASSERT_NE(sysmem_buffer, nullptr);
        EXPECT_EQ(sysmem_buffer->get_communication_id(), expected_id);
    }
}

// Host-side copies must land at the user's VA, not at the page-aligned start the buffer
// pins internally. This uses a deliberately unaligned mapping to exercise that.
TEST(ApiSysmemManager, SysmemBufferHostCopyUnaligned) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const size_t mmap_size = 2 * sysconf(_SC_PAGESIZE);
    constexpr size_t buf_offset = 10;  // Deliberately not page aligned.
    constexpr size_t buf_size = 128;

    void* mapping = mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);

    void* mapped_buffer = static_cast<uint8_t*>(mapping) + buf_offset;

    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapped_buffer, buf_size);

    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    sysmem_buffer->write_to_sysmem(pattern.data(), pattern.size(), 0);
    // The bytes must be visible at the caller's VA, which is the unaligned one.
    EXPECT_EQ(0, std::memcmp(mapped_buffer, pattern.data(), pattern.size()));

    std::vector<uint8_t> readback(pattern.size(), 0);
    sysmem_buffer->read_from_sysmem(readback.data(), readback.size(), 0);
    EXPECT_EQ(pattern, readback);

    constexpr size_t offset = 64;
    sysmem_buffer->write_to_sysmem(pattern.data(), pattern.size(), offset);
    std::fill(readback.begin(), readback.end(), 0);
    sysmem_buffer->read_from_sysmem(readback.data(), readback.size(), offset);
    EXPECT_EQ(pattern, readback);

    // Bounds are the user-requested size, not the larger page-aligned mapping.
    EXPECT_THROW(sysmem_buffer->write_to_sysmem(pattern.data(), 1, buf_size), std::exception);
    EXPECT_THROW(sysmem_buffer->read_from_sysmem(readback.data(), 2, buf_size - 1), std::exception);

    sysmem_buffer.reset();
    munmap(mapping, mmap_size);
}

TEST(ApiSysmemManager, SysmemBufferNocAddress) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    constexpr uint32_t one_mb = 1 << 20;
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(one_mb, true);

    EXPECT_TRUE(sysmem_buffer->get_noc_address().has_value());

    // The host memory channels are mapped by cluster->start_device, which wasn't called yet. So this will be the
    // first buffer that was mapped, and it is expected to have the starting NOC address.
    EXPECT_EQ(sysmem_buffer->get_noc_address().value(), cluster->get_sysmem_window_noc_base(mmio_chip));

    uint8_t* sysmem_data = static_cast<uint8_t*>(sysmem_buffer->get_va());
    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data[i] = 0;
    }

    // Pattern to write to sysmem buffer over NOC.
    std::vector<uint8_t> data_write(one_mb, 0);
    for (uint32_t i = 0; i < one_mb; ++i) {
        data_write[i] = static_cast<uint8_t>(i % 256);
    }

    // Write to sysmem buffer using NOC address.
    const CoreCoord pcie_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::PCIE)[0];
    cluster->write_to_device(
        data_write.data(), data_write.size(), mmio_chip, pcie_core, sysmem_buffer->get_noc_address().value());

    // Perform a read so we're sure that the write object has been flushed to the device.
    std::vector<uint8_t> readback(one_mb, 0);
    // Read back from sysmem buffer using NOC address.
    cluster->read_from_device(readback.data(), mmio_chip, pcie_core, sysmem_buffer->get_noc_address().value(), one_mb);
    EXPECT_EQ(readback, data_write);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], data_write[i])
            << "Mismatch at index " << i << ": expected " << static_cast<int>(data_write[i]) << ", got "
            << static_cast<int>(sysmem_data[i]);
    }

    // If we map another buffer it is expected to have a higher NOC address.
    std::unique_ptr<SysmemBuffer> sysmem_buffer2 = sysmem_manager->allocate_sysmem_buffer(one_mb, true);
    EXPECT_TRUE(sysmem_buffer2->get_noc_address().has_value());
    EXPECT_GT(sysmem_buffer2->get_noc_address().value(), cluster->get_sysmem_window_noc_base(mmio_chip));
}

// A buffer mapped mid-page reports the NOC address of its own first byte: a NOC write there lands at
// get_va() and leaves the bytes before it in the same page untouched.
TEST(ApiSysmemManager, UnalignedBufferNocAddressNamesItsFirstByte) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    // Two zero-filled pages, so a page-sized buffer starting 64 bytes in crosses into the second.
    const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void* mapping =
        mmap(nullptr, 2 * page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    uint8_t* page_start = static_cast<uint8_t*>(mapping);

    std::unique_ptr<SysmemBuffer> sysmem_buffer =
        sysmem_manager->map_user_buffer(page_start + 64, page_size, /*bind_to_noc=*/true);
    ASSERT_EQ(sysmem_buffer->get_va(), page_start + 64);
    ASSERT_TRUE(sysmem_buffer->get_noc_address().has_value());

    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    const CoreCoord pcie_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::PCIE)[0];
    const uint64_t noc_address = sysmem_buffer->get_noc_address().value();
    cluster->write_to_device(pattern.data(), pattern.size(), mmio_chip, pcie_core, noc_address);
    // Read back over the same path so the write is flushed before the host checks memory.
    std::vector<uint8_t> readback(pattern.size(), 0);
    cluster->read_from_device(readback.data(), mmio_chip, pcie_core, noc_address, readback.size());

    const uint8_t* buffer_start = static_cast<uint8_t*>(sysmem_buffer->get_va());
    EXPECT_EQ(std::vector<uint8_t>(buffer_start, buffer_start + pattern.size()), pattern);
    EXPECT_EQ(std::vector<uint8_t>(page_start, page_start + 64), std::vector<uint8_t>(64, 0));

    sysmem_buffer.reset();
    munmap(mapping, 2 * page_size);
}

// allocate_buffer returns exclusively owned memory, so buffers from two allocators on one device never
// alias, whatever backs them (a hugepage without an IOMMU, IOMMU-mapped pages with one). The allocators
// have no host memory channels, so only the buffers themselves are involved.
TEST(ApiSysmemManager, BuffersFromTwoAllocatorsOnOneDeviceAreDistinct) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_ids[0]);
    SiliconSysmemManager first_allocator(tt_device.get());
    SiliconSysmemManager second_allocator(tt_device.get());

    std::unique_ptr<SysmemBuffer> first_buffer = first_allocator.allocate_buffer(4096);
    std::unique_ptr<SysmemBuffer> second_buffer = second_allocator.allocate_buffer(4096);

    const std::vector<uint8_t> first_pattern(64, 0xAA);
    const std::vector<uint8_t> second_pattern(64, 0x55);
    first_buffer->write_to_sysmem(first_pattern.data(), first_pattern.size(), 0);
    second_buffer->write_to_sysmem(second_pattern.data(), second_pattern.size(), 0);

    std::vector<uint8_t> first_readback(64, 0);
    std::vector<uint8_t> second_readback(64, 0);
    first_buffer->read_from_sysmem(first_readback.data(), first_readback.size(), 0);
    second_buffer->read_from_sysmem(second_readback.data(), second_readback.size(), 0);
    EXPECT_EQ(first_readback, std::vector<uint8_t>(64, 0xAA));
    EXPECT_EQ(second_readback, std::vector<uint8_t>(64, 0x55));
}

// NOC binding is opt-in: a buffer allocated without bind_to_noc has no NOC address.
TEST(ApiSysmemManager, AllocatedBufferIsNotNocBoundByDefault) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_ids[0]);
    SiliconSysmemManager allocator(tt_device.get());

    std::unique_ptr<SysmemBuffer> buffer = allocator.allocate_buffer(4096);

    EXPECT_FALSE(buffer->get_noc_address().has_value());
}

// A buffer allocated with bind_to_noc is reachable from the device: a NOC write to its address lands at
// get_va(). Runs with and without an IOMMU.
TEST(ApiSysmemManager, AllocatedBufferBoundToNocIsReachableOverNoc) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_buffer(1 << 20, /*bind_to_noc=*/true);
    ASSERT_TRUE(sysmem_buffer->get_noc_address().has_value());

    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    const CoreCoord pcie_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::PCIE)[0];
    const uint64_t noc_address = sysmem_buffer->get_noc_address().value();
    cluster->write_to_device(pattern.data(), pattern.size(), mmio_chip, pcie_core, noc_address);
    // Read back over the same path so the write is flushed before the host checks memory.
    std::vector<uint8_t> readback(pattern.size(), 0);
    cluster->read_from_device(readback.data(), mmio_chip, pcie_core, noc_address, readback.size());

    const uint8_t* buffer_start = static_cast<uint8_t*>(sysmem_buffer->get_va());
    EXPECT_EQ(std::vector<uint8_t>(buffer_start, buffer_start + pattern.size()), pattern);
}

// Without an IOMMU the device reaches only physically contiguous memory, so each buffer is one 1 GB
// hugepage and 1 GB is the largest size allocate_buffer accepts.
TEST(ApiSysmemManager, AllocationAboveOneGibThrowsWithoutIommu) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "The 1 GB limit applies only without an IOMMU.";
    }
    std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_ids[0]);
    SiliconSysmemManager allocator(tt_device.get());

    EXPECT_THROW(allocator.allocate_buffer((1ULL << 30) + 4096), std::runtime_error);
}

// Without an IOMMU a buffer of exactly 1 GB fills one hugepage, the largest allocation allowed.
TEST(ApiSysmemManager, OneGibAllocationSucceedsWithoutIommu) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "The 1 GB limit applies only without an IOMMU.";
    }
    std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_ids[0]);
    SiliconSysmemManager allocator(tt_device.get());

    EXPECT_NO_THROW(allocator.allocate_buffer(1ULL << 30));
}

// Without an IOMMU each buffer takes a whole 1 GB hugepage, so destroying it must give the page back:
// allocating more buffers one after another than the host has 1 GB hugepages succeeds only if none leaks.
TEST(ApiSysmemManager, DestroyedBufferReturnsItsHugepage) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Buffers are hugepage-backed only without an IOMMU.";
    }
    std::unique_ptr<TTDevice> tt_device = create_tt_device(pci_device_ids[0]);
    SiliconSysmemManager allocator(tt_device.get());

    // A loop bound, not an expected value: one more cycle than the pool has pages.
    size_t nr_hugepages = 0;
    std::ifstream("/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages") >> nr_hugepages;

    for (size_t i = 0; i <= nr_hugepages; i++) {
        ASSERT_NO_THROW(allocator.allocate_buffer(4096)) << "allocation " << i << " of " << nr_hugepages + 1;
    }
}

TEST(ApiSysmemManager, ReadOnlySharedFileMapping) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    if (cluster->get_target_mmio_device_ids().empty()) {
        GTEST_SKIP() << "No Tenstorrent MMIO devices found.";
    }
    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    // Probe the capability on the manager the mapping will actually be made through, so a device other than the one
    // map_sysmem_buffer ends up on cannot clear the gate and turn a skip into a throw.
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();
    if (!sysmem_manager->is_read_only_page_pinning_supported()) {
        GTEST_SKIP() << "Device-read-only page pinning requires an active IOMMU and KMD "
                     << KMD_READ_ONLY_PAGE_PINNING.str() << " or newer.";
    }

    const size_t mapping_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const std::string file_template = (std::filesystem::temp_directory_path() / "tt_umd_read_only_pin_XXXXXX").string();
    std::vector<char> file_name(file_template.begin(), file_template.end());
    file_name.push_back('\0');
    int writable_fd = mkstemp(file_name.data());
    ASSERT_NE(writable_fd, -1);

    // Remove the file on every exit path, including an ASSERT failure before the explicit unlink below.
    struct FileRemover {
        std::string path;

        ~FileRemover() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } file_remover{std::string(file_name.data())};

    ASSERT_EQ(ftruncate(writable_fd, mapping_size), 0);

    // Seed the file with a position-dependent pattern while it is still writable, so the transfer below proves the
    // device actually reads the pinned pages rather than just that the accessors echo their arguments back.
    void* writable_mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, writable_fd, 0);
    ASSERT_NE(writable_mapping, MAP_FAILED);
    std::vector<uint8_t> expected(mapping_size);
    for (size_t i = 0; i < mapping_size; ++i) {
        expected[i] = static_cast<uint8_t>((i * 31 + 7) % 256);
    }
    std::memcpy(writable_mapping, expected.data(), mapping_size);
    ASSERT_EQ(munmap(writable_mapping, mapping_size), 0);
    ASSERT_EQ(close(writable_fd), 0);

    int read_only_fd = open(file_name.data(), O_RDONLY | O_CLOEXEC);
    ASSERT_NE(read_only_fd, -1);
    ASSERT_EQ(unlink(file_name.data()), 0);

    void* mapping = mmap(nullptr, mapping_size, PROT_READ, MAP_SHARED, read_only_fd, 0);
    ASSERT_NE(mapping, MAP_FAILED);

    auto sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping, mapping_size, true, DeviceBufferAccess::READ_ONLY);

    ASSERT_NE(sysmem_buffer, nullptr);
    EXPECT_EQ(sysmem_buffer->get_va(), mapping);
    EXPECT_EQ(sysmem_buffer->get_size(), mapping_size);
    EXPECT_EQ(sysmem_buffer->get_device_access(), DeviceBufferAccess::READ_ONLY);
    EXPECT_TRUE(sysmem_buffer->get_noc_address().has_value());

    // Device reads the read-only mapping and writes it into Tensix L1 -- the direction read-only pinning exists to
    // serve. Reading it back independently confirms the mapping is genuinely usable, not merely accepted.
    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];
    const xy_pair tensix_core_xy =
        cluster->get_soc_descriptor(mmio_chip).translate_chip_coord_to_translated(tensix_core, get_selected_noc_id());
    std::vector<uint8_t> zeros(mapping_size, 0);
    cluster->write_to_device(zeros.data(), mapping_size, mmio_chip, tensix_core, 0);
    sysmem_buffer->dma_write_to_device(0, mapping_size, tensix_core_xy, 0);

    // Read back over MMIO rather than DMA: D2H DMA is unsupported on Blackhole, and the direction under test here
    // is the device-side read of the pinned mapping, not how the host retrieves the result.
    std::vector<uint8_t> readback(mapping_size, 0);
    cluster->read_from_device(readback.data(), mmio_chip, tensix_core, 0, mapping_size);
    EXPECT_EQ(readback, expected);

    // The opposite direction writes host memory, so it must be refused on a device-read-only mapping.
    EXPECT_THROW(sysmem_buffer->dma_read_from_device(0, mapping_size, tensix_core_xy, 0), std::exception);

    sysmem_buffer.reset();
    EXPECT_EQ(munmap(mapping, mapping_size), 0);
    EXPECT_EQ(close(read_only_fd), 0);
}

TEST(ApiSysmemManager, AutoNumChannels) {
    // Automatically set number of host memory channels.
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = std::nullopt});
    bool has_n300 = false;  // Only supported HW with two chips per PCIe channel.
    for (auto device_id : cluster->get_target_mmio_device_ids()) {
        has_n300 |= (cluster->get_tt_device(device_id)->get_board_type() == tt::N300);
    }
    const uint32_t expected_channels = has_n300 ? 2 : 1;
    for (auto device_id : cluster->get_target_mmio_device_ids()) {
        EXPECT_EQ(cluster->get_num_host_channels(device_id), expected_channels);
    }
}
