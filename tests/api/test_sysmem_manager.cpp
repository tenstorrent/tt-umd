// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"

#include <gtest/gtest.h>
#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace tt::umd;

const uint32_t HUGEPAGE_REGION_SIZE = 1ULL << 30;  // 1GB

TEST(ApiSysmemManager, BasicIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);

        std::unique_ptr<TLBManager> tlb_manager = std::make_unique<TLBManager>(tt_device.get());

        // Initializes system memory with one channel.
        std::unique_ptr<SysmemManager> sysmem = std::make_unique<SiliconSysmemManager>(tlb_manager.get(), 1);

        sysmem->pin_or_map_sysmem_to_device();

        // Simple write and read test.
        std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        sysmem->write_to_sysmem(0, data_write.data(), 0, data_write.size() * sizeof(uint32_t));

        std::vector<uint32_t> data_read = std::vector<uint32_t>(data_write.size(), 0);
        sysmem->read_from_sysmem(0, data_read.data(), 0, data_read.size() * sizeof(uint32_t));

        EXPECT_EQ(data_write, data_read);

        // Channel 1 is not setup, so this should throw an exception.
        EXPECT_THROW(
            sysmem->write_to_sysmem(1, data_write.data(), 0, data_write.size() * sizeof(uint32_t)), std::runtime_error);

        // When we write over the limit, the address is wrapped around the hugepage size.
        sysmem->write_to_sysmem(
            0, data_write.data(), HUGEPAGE_REGION_SIZE + 0x100, data_write.size() * sizeof(uint32_t));
        data_read = std::vector<uint32_t>(data_write.size(), 0);
        sysmem->read_from_sysmem(0, data_read.data(), 0x100, data_read.size() * sizeof(uint32_t));
        EXPECT_EQ(data_write, data_read);
    }
}

TEST(ApiSysmemManager, SysmemBuffers) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    std::unique_ptr<PCIDevice> pci_device = std::make_unique<PCIDevice>(pci_device_ids[0]);

    if (!pci_device->is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    if (pci_device->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole, as PCIE DMA is not supported on Blackhole.";
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const uint32_t one_mb = 1 << 20;
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(2 * one_mb);

    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];

    // Zero out 1MB of Tensix L1.
    std::vector<uint8_t> data_write(one_mb, 0);
    cluster->write_to_device(data_write.data(), one_mb, mmio_chip, tensix_core, 0);

    uint8_t* sysmem_data = static_cast<uint8_t*>(sysmem_buffer->get_buffer_va());

    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data[i] = static_cast<uint8_t>(i % 256);
    }

    // Write pattern to first 1MB of Tensix L1.
    sysmem_buffer->dma_write_to_device(0, one_mb, tensix_core, 0);

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
    sysmem_buffer->dma_read_from_device(one_mb, one_mb, tensix_core, 0);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], sysmem_data_readback[i]);
    }
}

TEST(ApiSysmemManager, SysmemBufferUnaligned) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    std::unique_ptr<PCIDevice> pci_device = std::make_unique<PCIDevice>(pci_device_ids[0]);
    if (!pci_device->is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    if (pci_device->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole, as PCIE DMA is not supported on Blackhole.";
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const uint32_t one_mb = 1 << 20;
    void* mapping =
        mmap(nullptr, 2 * one_mb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    // It's important that this offset is not a multiple of the page size.
    const size_t unaligned_offset = 100;
    void* mapping_buffer = static_cast<uint8_t*>(mapping) + unaligned_offset;  // Offset by 1MB

    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping_buffer, one_mb);

    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];

    // Zero out 1MB of Tensix L1.
    std::vector<uint8_t> data_write(one_mb, 0);
    cluster->write_to_device(data_write.data(), one_mb, mmio_chip, tensix_core, 0);

    uint8_t* sysmem_data = static_cast<uint8_t*>(sysmem_buffer->get_buffer_va());

    EXPECT_EQ(sysmem_data, mapping_buffer);
    EXPECT_EQ(sysmem_buffer->get_buffer_size(), one_mb);

    for (uint32_t i = 0; i < one_mb; ++i) {
        sysmem_data[i] = static_cast<uint8_t>(i % 256);
    }

    // Write pattern to first 1MB of Tensix L1.
    sysmem_buffer->dma_write_to_device(0, one_mb, tensix_core, 0);

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
    sysmem_buffer->dma_read_from_device(0, one_mb, tensix_core, 0);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], readback[i]);
    }
}

TEST(ApiSysmemManager, SysmemBufferFunctions) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const size_t mmap_size = 20;
    const size_t buf_size = 10;

    // Size is not multiple of page size.
    void* mapping = mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    void* mapped_buffer = static_cast<uint8_t*>(mapping) + buf_size;  // Offset by 10 bytes

    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapped_buffer, buf_size);

    EXPECT_EQ(sysmem_buffer->get_buffer_size(), buf_size);
    EXPECT_EQ(sysmem_buffer->get_buffer_va(), mapped_buffer);
}

TEST(ApiSysmemManager, SysmemBufferNocAddress) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_mapping_buffer_to_noc_supported()) {
        GTEST_SKIP() << "Skipping test since KMD doesn't support noc address mapping.";
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const uint32_t one_mb = 1 << 20;
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(one_mb, true);

    EXPECT_TRUE(sysmem_buffer->get_noc_addr().has_value());

    // We haven't actually mapped the hugepage yet, since cluster->start_device or
    // sysmem_manager->pin_or_map_sysmem_to_device wasn't called yet. So this will be the first buffer that was mapped,
    // and it is expected to have the starting NOC address.
    EXPECT_EQ(sysmem_buffer->get_noc_addr().value(), cluster->get_pcie_base_addr_from_device(mmio_chip));

    uint8_t* sysmem_data = static_cast<uint8_t*>(sysmem_buffer->get_buffer_va());
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
        data_write.data(), data_write.size(), mmio_chip, pcie_core, sysmem_buffer->get_noc_addr().value());

    // Perform a read so we're sure that the write object has been flushed to the device.
    std::vector<uint8_t> readback(one_mb, 0);
    // Read back from sysmem buffer using NOC address.
    cluster->read_from_device(readback.data(), mmio_chip, pcie_core, sysmem_buffer->get_noc_addr().value(), one_mb);
    EXPECT_EQ(readback, data_write);

    for (uint32_t i = 0; i < one_mb; ++i) {
        ASSERT_EQ(sysmem_data[i], data_write[i])
            << "Mismatch at index " << i << ": expected " << static_cast<int>(data_write[i]) << ", got "
            << static_cast<int>(sysmem_data[i]);
    }

    // If we map another buffer it is expected to have a higher NOC address.
    std::unique_ptr<SysmemBuffer> sysmem_buffer2 = sysmem_manager->allocate_sysmem_buffer(one_mb, true);
    EXPECT_TRUE(sysmem_buffer2->get_noc_addr().has_value());
    EXPECT_GT(sysmem_buffer2->get_noc_addr().value(), cluster->get_pcie_base_addr_from_device(mmio_chip));
}
