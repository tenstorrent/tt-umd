// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/firmware_telemetry/arc_telemetry_reader.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/cluster_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/utils/semver.hpp"

using namespace tt;
using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy.

// This test should be one line only.
TEST(ApiClusterTest, OpenAllSiliconChips) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();
}

TEST(TestCluster, PrintAllSiliconChipsAllCores) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

    for (ChipId chip : umd_cluster->get_target_device_ids()) {
        std::cout << "Chip " << chip << std::endl;

        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip);

        const std::vector<CoreCoord>& tensix_cores = soc_desc.get_cores(CoreType::TENSIX);
        for (const CoreCoord& core : tensix_cores) {
            std::cout << "Tensix core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_tensix_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);
        for (const CoreCoord& core : harvested_tensix_cores) {
            std::cout << "Harvested Tensix core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& dram_cores = soc_desc.get_cores(CoreType::DRAM);
        for (const CoreCoord& core : dram_cores) {
            std::cout << "DRAM core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_dram_cores = soc_desc.get_harvested_cores(CoreType::DRAM);
        for (const CoreCoord& core : harvested_dram_cores) {
            std::cout << "Harvested DRAM core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& eth_cores = soc_desc.get_cores(CoreType::ETH);
        for (const CoreCoord& core : eth_cores) {
            std::cout << "ETH core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_eth_cores = soc_desc.get_harvested_cores(CoreType::ETH);
        for (const CoreCoord& core : harvested_eth_cores) {
            std::cout << "Harvested ETH core " << core.str() << std::endl;
        }
    }
}

TEST(TestCluster, TestClusterAICLKControl) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    auto get_expected_clock_val = [&cluster](ChipId chip_id, bool busy) {
        tt::ARCH arch = cluster->get_cluster_description()->get_arch(chip_id);
        if (arch == tt::ARCH::WORMHOLE_B0) {
            return busy ? wormhole::AICLK_BUSY_VAL : wormhole::AICLK_IDLE_VAL;
        } else if (arch == tt::ARCH::BLACKHOLE) {
            return busy ? blackhole::AICLK_BUSY_VAL : blackhole::AICLK_IDLE_VAL;
        }
        return 0u;
    };

    cluster->set_clock_state(DevicePowerState::BUSY);

    auto clocks_busy = cluster->get_clocks();
    for (auto& clock : clocks_busy) {
        // TODO #781: Figure out a proper mechanism to detect the right value. For now just check that Busy value is
        // larger than Idle value.
        EXPECT_GT(clock.second, get_expected_clock_val(clock.first, false));
    }

    cluster->set_clock_state(DevicePowerState::LONG_IDLE);

    auto clocks_idle = cluster->get_clocks();
    for (auto& clock : clocks_idle) {
        EXPECT_EQ(clock.second, get_expected_clock_val(clock.first, false));
    }
}

TEST(TestCluster, RefreshClusterDescriptionDoesNotThrow) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    EXPECT_NO_THROW(cluster->refresh_cluster_description());
}

TEST(TestCluster, GetEthernetFirmware) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    // BoardType P100 doesn't have eth cores.
    std::optional<SemVer> eth_version;
    EXPECT_NO_THROW(eth_version = cluster->get_ethernet_firmware_version());
    if (cluster->get_cluster_description()->get_board_type(0) == BoardType::P100) {
        EXPECT_FALSE(eth_version.has_value());
    } else {
        EXPECT_TRUE(eth_version.has_value());
    }
}

TEST(TestCluster, TestDifferentPowerModes) {
    {
        if (PCIDevice::get_pcie_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Different power modes is supported only for Blackhole.";
        }
    }

    {
        TopologyDiscoveryOptions default_options;
        auto [desc_default, devices_default] = TopologyDiscovery::discover(default_options);
        for (auto& [chip_id, tt_device] : devices_default) {
            FirmwareTelemetryReader* telemetry_reader = tt_device->get_firmware_telemetry_reader();
            uint32_t power = telemetry_reader->read_entry(TelemetryTag::INPUT_POWER);
            std::cout << "Default mode - Chip " << chip_id << " power: " << power << std::endl;
        }
    }

    {
        TopologyDiscoveryOptions power_options;
        power_options.low_power = true;
        auto [desc_low_power, devices_low_power] = TopologyDiscovery::discover(power_options);
        for (auto& [chip_id, tt_device] : devices_low_power) {
            FirmwareTelemetryReader* telemetry_reader = tt_device->get_firmware_telemetry_reader();
            uint32_t power = telemetry_reader->read_entry(TelemetryTag::INPUT_POWER);
            std::cout << "Low power mode - Chip " << chip_id << " power: " << power << std::endl;
        }
    }

    {
        std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
        for (ChipId chip_id : cluster->get_target_device_ids()) {
            TTDevice* tt_device = cluster->get_tt_device(chip_id);
            FirmwareTelemetryReader* telemetry_reader = tt_device->get_firmware_telemetry_reader();
            uint32_t power = telemetry_reader->read_entry(TelemetryTag::INPUT_POWER);
            std::cout << "Cluster mode - Chip " << chip_id << " power: " << power << std::endl;
        }
    }
}

// Host memory channels as tt-metal uses them: a started cluster with one channel per MMIO chip.
class ApiClusterHostMemChannels : public ::testing::Test {
protected:
    void SetUp() override {
        if (PCIDevice::enumerate_devices().empty()) {
            GTEST_SKIP() << "No Tenstorrent PCI devices found.";
        }
        cluster_ = test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
        test_utils::safe_test_cluster_start(cluster_.get());
    }

    void TearDown() override {
        if (cluster_) {
            cluster_->close_device();
        }
    }

    std::unique_ptr<Cluster> cluster_;
};

TEST_F(ApiClusterHostMemChannels, RoundTripThroughChannel) {
    const std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const uint32_t size = static_cast<uint32_t>(data_write.size() * sizeof(uint32_t));

    for (const ChipId chip : cluster_->get_target_mmio_device_ids()) {
        cluster_->write_to_sysmem(data_write.data(), size, /*addr=*/0, /*channel=*/0, chip);
        std::vector<uint32_t> data_read(data_write.size(), 0);
        cluster_->read_from_sysmem(data_read.data(), /*addr=*/0, /*channel=*/0, size, chip);

        EXPECT_EQ(data_read, (std::vector<uint32_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10})) << "chip " << chip;
    }
}

TEST_F(ApiClusterHostMemChannels, MissingChannelThrows) {
    const std::vector<uint32_t> data_write = {1, 2, 3, 4};

    for (const ChipId chip : cluster_->get_target_mmio_device_ids()) {
        EXPECT_THROW(
            cluster_->write_to_sysmem(
                data_write.data(),
                static_cast<uint32_t>(data_write.size() * sizeof(uint32_t)),
                /*addr=*/0,
                /*channel=*/1,
                chip),
            std::runtime_error)
            << "chip " << chip;
    }
}

// Channel 0 is 1 GiB, so an address past its end wraps around to the channel's start.
TEST_F(ApiClusterHostMemChannels, OffsetWrapsAtChannelSize) {
    const std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const uint32_t size = static_cast<uint32_t>(data_write.size() * sizeof(uint32_t));

    for (const ChipId chip : cluster_->get_target_mmio_device_ids()) {
        cluster_->write_to_sysmem(data_write.data(), size, /*addr=*/0x40000100, /*channel=*/0, chip);
        std::vector<uint32_t> data_read(data_write.size(), 0);
        cluster_->read_from_sysmem(data_read.data(), /*addr=*/0x100, /*channel=*/0, size, chip);

        EXPECT_EQ(data_read, (std::vector<uint32_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10})) << "chip " << chip;
    }
}

// Channels exist only between start_device and close_device.
TEST(ApiClusterHostMemChannelSetup, ChannelsAreMappedOnlyWhileStarted) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    const ChipId chip = *cluster->get_target_mmio_device_ids().begin();

    EXPECT_EQ(cluster->host_dma_address(/*offset=*/0, chip, /*channel=*/0), nullptr);

    test_utils::safe_test_cluster_start(cluster.get());
    EXPECT_NE(cluster->host_dma_address(/*offset=*/0, chip, /*channel=*/0), nullptr);

    cluster->close_device();
    EXPECT_EQ(cluster->host_dma_address(/*offset=*/0, chip, /*channel=*/0), nullptr);
}

// Closing a cluster unmaps its channels, and starting it again maps them again.
TEST(ApiClusterHostMemChannelSetup, ChannelsSurviveRestart) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    const ChipId chip = *cluster->get_target_mmio_device_ids().begin();
    test_utils::safe_test_cluster_start(cluster.get());
    cluster->close_device();
    test_utils::safe_test_cluster_start(cluster.get());

    const std::vector<uint32_t> data_write = {0xCAFEF00D, 0x12345678};
    const uint32_t size = static_cast<uint32_t>(data_write.size() * sizeof(uint32_t));
    cluster->write_to_sysmem(data_write.data(), size, /*addr=*/0, /*channel=*/0, chip);
    std::vector<uint32_t> data_read(data_write.size(), 0);
    cluster->read_from_sysmem(data_read.data(), /*addr=*/0, /*channel=*/0, size, chip);

    EXPECT_EQ(data_read, (std::vector<uint32_t>{0xCAFEF00D, 0x12345678}));
    cluster->close_device();
}

// Channel N sits at noc_base + N * 1 GiB. Hosts without enough hugepages plan fewer channels than requested.
TEST(ApiClusterHostMemChannelSetup, HostWriteToChannelOneIsReadAtNocBasePlusOneGib) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (test_utils::is_virtual_machine()) {
        GTEST_SKIP() << "VM runners cannot hold the DMA mappings of several channels.";
    }
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 2});
    const ChipId chip = *cluster->get_target_mmio_device_ids().begin();
    if (cluster->get_num_host_channels(chip) < 2) {
        GTEST_SKIP() << "Fewer than 2 host memory channels are available.";
    }
    test_utils::safe_test_cluster_start(cluster.get());

    const std::vector<uint32_t> data_write = {0xA1B2C3D4, 0x11223344, 0x55667788, 0x99AABBCC};
    const uint32_t size = static_cast<uint32_t>(data_write.size() * sizeof(uint32_t));
    cluster->write_to_sysmem(data_write.data(), size, /*addr=*/0, /*channel=*/1, chip);

    const CoreCoord pcie_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::PCIE)[0];
    std::vector<uint32_t> data_read(data_write.size(), 0);
    cluster->read_from_device(
        data_read.data(), chip, pcie_core, cluster->get_sysmem_window_noc_base(chip) + 0x40000000, size);

    EXPECT_EQ(data_read, (std::vector<uint32_t>{0xA1B2C3D4, 0x11223344, 0x55667788, 0x99AABBCC}));
    cluster->close_device();
}

TEST(ApiClusterHostMemChannelSetup, NocWriteAtNocBasePlusOneGibLandsInChannelOne) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (test_utils::is_virtual_machine()) {
        GTEST_SKIP() << "VM runners cannot hold the DMA mappings of several channels.";
    }
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 2});
    const ChipId chip = *cluster->get_target_mmio_device_ids().begin();
    if (cluster->get_num_host_channels(chip) < 2) {
        GTEST_SKIP() << "Fewer than 2 host memory channels are available.";
    }
    test_utils::safe_test_cluster_start(cluster.get());

    const std::vector<uint32_t> data_write = {0xA1B2C3D4, 0x11223344, 0x55667788, 0x99AABBCC};
    const uint32_t size = static_cast<uint32_t>(data_write.size() * sizeof(uint32_t));
    const CoreCoord pcie_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::PCIE)[0];
    const uint64_t channel_one_noc_address = cluster->get_sysmem_window_noc_base(chip) + 0x40000000;
    cluster->write_to_device(data_write.data(), size, chip, pcie_core, channel_one_noc_address);
    // Read back over the same path so the write is flushed before the host checks memory.
    std::vector<uint32_t> flush(data_write.size(), 0);
    cluster->read_from_device(flush.data(), chip, pcie_core, channel_one_noc_address, size);

    std::vector<uint32_t> data_read(data_write.size(), 0);
    cluster->read_from_sysmem(data_read.data(), /*addr=*/0, /*channel=*/1, size, chip);

    EXPECT_EQ(data_read, (std::vector<uint32_t>{0xA1B2C3D4, 0x11223344, 0x55667788, 0x99AABBCC}));
    cluster->close_device();
}

// On Wormhole the last of four channels is 768 MB rather than a full 1 GiB.
TEST(ApiClusterHostMemChannelSetup, WormholeChannelThreeIs768Mb) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (test_utils::is_virtual_machine()) {
        GTEST_SKIP() << "VM runners cannot hold the DMA mappings of several channels.";
    }
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 4});
    const ChipId chip = *cluster->get_target_mmio_device_ids().begin();
    if (cluster->get_soc_descriptor(chip).arch != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "Only Wormhole limits channel 3.";
    }
    if (cluster->get_num_host_channels(chip) < 4) {
        GTEST_SKIP() << "Fewer than 4 host memory channels are available.";
    }
    test_utils::safe_test_cluster_start(cluster.get());

    EXPECT_EQ(cluster->get_host_channel_size(chip, 0), 0x40000000u);
    EXPECT_EQ(cluster->get_host_channel_size(chip, 3), 0x30000000u);
    cluster->close_device();
}

// Without an IOMMU each channel is the device's own hugetlbfs page for that channel, shared by every process using
// the device, so a buffer allocated before the channels are mapped must not take a channel's page. PCI device 0's
// channel 0 is the file /dev/hugepages-1G/tenstorrent.
TEST(ApiClusterHostMemChannelSetup, ChannelMapsItsHugepageFileAfterAnUnrelatedBuffer) {
    if (PCIDevice::enumerate_devices().empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (test_utils::is_iommu_available()) {
        GTEST_SKIP() << "Channels are hugepage files only without an IOMMU.";
    }
    std::unique_ptr<Cluster> cluster =
        test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    std::optional<ChipId> device_0_chip;
    for (const auto& [chip, pci_device_id] : cluster->get_cluster_description()->get_chips_with_mmio()) {
        if (pci_device_id == 0) {
            device_0_chip = chip;
        }
    }
    if (!device_0_chip.has_value()) {
        GTEST_SKIP() << "PCI device 0 is not visible.";
    }
    const ChipId chip = *device_0_chip;

    std::unique_ptr<SysmemBuffer> unrelated_buffer =
        cluster->get_chip(chip)->get_sysmem_manager()->allocate_buffer(4096);
    test_utils::safe_test_cluster_start(cluster.get());

    const std::vector<uint8_t> channel_pattern = {0xA5, 0x5A, 0xC3, 0x3C};
    cluster->write_to_sysmem(
        channel_pattern.data(), static_cast<uint32_t>(channel_pattern.size()), /*addr=*/0, /*channel=*/0, chip);
    const std::vector<uint8_t> buffer_pattern = {0x11, 0x22, 0x33, 0x44};
    unrelated_buffer->write_to_sysmem(buffer_pattern.data(), buffer_pattern.size(), /*offset=*/0);

    const int fd = open("/dev/hugepages-1G/tenstorrent", O_RDWR);
    ASSERT_NE(fd, -1) << "PCI device 0's channel 0 hugepage file is missing.";
    constexpr size_t one_gib = 1ULL << 30;
    void* channel_file = mmap(nullptr, one_gib, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    ASSERT_NE(channel_file, MAP_FAILED);
    uint8_t* channel_bytes = static_cast<uint8_t*>(channel_file);

    // The channel and the file are the same memory, and the unrelated buffer is neither.
    EXPECT_EQ(std::vector<uint8_t>(channel_bytes, channel_bytes + 4), (std::vector<uint8_t>{0xA5, 0x5A, 0xC3, 0x3C}));
    std::vector<uint8_t> buffer_read(4, 0);
    unrelated_buffer->read_from_sysmem(buffer_read.data(), buffer_read.size(), /*offset=*/0);
    EXPECT_EQ(buffer_read, (std::vector<uint8_t>{0x11, 0x22, 0x33, 0x44}));

    // The reverse direction too, which no content left in the file by an earlier process can satisfy.
    const std::vector<uint8_t> file_pattern = {0x5C, 0x3A, 0xA5, 0xC3};
    std::copy(file_pattern.begin(), file_pattern.end(), channel_bytes);
    std::vector<uint8_t> channel_read(4, 0);
    cluster->read_from_sysmem(
        channel_read.data(), /*addr=*/0, /*channel=*/0, static_cast<uint32_t>(channel_read.size()), chip);
    EXPECT_EQ(channel_read, (std::vector<uint8_t>{0x5C, 0x3A, 0xA5, 0xC3}));

    munmap(channel_file, one_gib);
    cluster->close_device();
}
