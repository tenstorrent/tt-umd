// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ios>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/tt_device/remote_communication_legacy_firmware.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

using namespace tt::umd;

TEST(RemoteCommunicationWormhole, BasicRemoteCommunicationIO) {
    const uint64_t address0 = 0x1000;
    const uint64_t address1 = 0x2000;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    ChipId mmio_chip_id = *cluster->get_target_mmio_device_ids().begin();
    LocalChip* local_chip = cluster->get_local_chip(mmio_chip_id);

    ClusterDescriptor* cluster_desc = cluster->get_cluster_description();

    std::vector<uint32_t> data_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> data_read(10, 0);

    std::vector<tt_xy_pair> active_eth_cores = {};
    if (cluster_desc->get_ethernet_connections().find(mmio_chip_id) == cluster_desc->get_ethernet_connections().end()) {
        GTEST_SKIP() << "No ethernet connections found for MMIO chip " << mmio_chip_id << ". Skipping the test.";
    }
    auto eth_connections_chip = cluster_desc->get_ethernet_connections().at(mmio_chip_id);
    for (const auto& [eth_channel, eth_connection] : eth_connections_chip) {
        CoreCoord logical_eth_core = CoreCoord(0, eth_channel, CoreType::ETH, CoordSystem::LOGICAL);
        CoreCoord noc0_eth_core =
            cluster->get_soc_descriptor(mmio_chip_id).translate_coord_to(logical_eth_core, CoordSystem::NOC0);
        active_eth_cores.push_back(tt_xy_pair(noc0_eth_core.x, noc0_eth_core.y));
    }

    for (ChipId remote_chip_id : cluster->get_target_remote_device_ids()) {
        EthCoord remote_eth_coord = cluster_desc->get_chip_locations().at(remote_chip_id);

        std::unique_ptr<RemoteCommunicationLegacyFirmware> remote_comm =
            std::make_unique<RemoteCommunicationLegacyFirmware>(
                local_chip->get_tt_device()->get_mmio_protocol(), remote_eth_coord, local_chip->get_sysmem_manager());
        remote_comm->set_remote_transfer_ethernet_cores(local_chip->get_soc_descriptor().get_eth_xy_pairs_for_channels(
            cluster->get_cluster_description()->get_active_eth_channels(mmio_chip_id), CoordSystem::TRANSLATED));

        for (const CoreCoord& core : cluster->get_soc_descriptor(remote_chip_id).get_cores(CoreType::TENSIX)) {
            CoreCoord translated_core =
                cluster->get_soc_descriptor(remote_chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
            remote_comm->write_to_non_mmio(
                translated_core, data_to_write.data(), address0, data_to_write.size() * sizeof(uint32_t));

            cluster->write_to_device(
                data_to_write.data(), data_to_write.size() * sizeof(uint32_t), remote_chip_id, core, address1);

            remote_comm->wait_for_non_mmio_flush();

            remote_comm->read_non_mmio(
                translated_core, data_read.data(), address1, data_read.size() * sizeof(uint32_t));

            ASSERT_EQ(data_to_write, data_read)
                << "Vector read back from core " << core.str() << " does not match what was written";

            data_read = std::vector<uint32_t>(10, 0);

            cluster->read_from_device(
                data_read.data(), remote_chip_id, core, address0, data_read.size() * sizeof(uint32_t));

            ASSERT_EQ(data_to_write, data_read)
                << "Vector read back from core " << core.str() << " does not match what was written";

            data_read = std::vector<uint32_t>(10, 0);

            for (int i = 0; i < 10; i++) {
                data_to_write[i] = data_to_write[i] + 10;
            }
        }
    }
}

// Test large transfers (> 1024 bytes) to remote chips without sysmem
// This test verifies that chunking works correctly when sysmem_manager is nullptr.
TEST(RemoteCommunicationWormhole, LargeTransferNoSysmem) {
    // Discover cluster topology.
    auto [cluster_desc, _] = TopologyDiscovery::discover(TopologyDiscoveryOptions{});

    // Find a remote chip.
    std::optional<ChipId> remote_chip_id;
    for (ChipId chip_id : cluster_desc->get_all_chips()) {
        if (!cluster_desc->is_chip_mmio_capable(chip_id)) {
            remote_chip_id = chip_id;
            break;
        }
    }

    if (!remote_chip_id.has_value()) {
        GTEST_SKIP() << "No remote chips found. Test requires at least one remote chip. Skipping test.";
    }

    ChipId local_chip_id = cluster_desc->get_closest_mmio_capable_chip(*remote_chip_id);
    int physical_device_id = cluster_desc->get_chips_with_mmio().at(local_chip_id);
    std::unique_ptr<TTDevice> local_tt_device = TTDevice::create(physical_device_id);
    local_tt_device->init_tt_device();

    SocDescriptor local_soc_descriptor = SocDescriptor(local_tt_device->get_arch(), local_tt_device->get_chip_info());
    EthCoord target_chip = cluster_desc->get_chip_locations().at(*remote_chip_id);
    auto remote_communication = RemoteCommunication::create_remote_communication(
        local_tt_device->get_mmio_protocol(), target_chip, nullptr);  // nullptr for sysmem_manager
    remote_communication->set_remote_transfer_ethernet_cores(
        local_soc_descriptor.get_eth_xy_pairs_for_channels(cluster_desc->get_active_eth_channels(local_chip_id)));
    std::unique_ptr<TTDevice> remote_tt_device = TTDevice::create(std::move(remote_communication));
    remote_tt_device->init_tt_device();

    // Get a tensix core to test on.
    SocDescriptor remote_soc_desc(remote_tt_device->get_arch(), remote_tt_device->get_chip_info());
    auto tensix_core = *remote_soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED).begin();
    tt_xy_pair tensix_core_xy = tt_xy_pair(tensix_core.x, tensix_core.y);

    // Test with 2048 bytes (2x the 1024 threshold).
    constexpr uint32_t test_size = 2048;
    constexpr uint64_t test_address = 0x100;

    // First write only zeros.
    std::vector<uint32_t> data_to_write(test_size / sizeof(uint32_t), 0);
    std::vector<uint32_t> data_read(test_size / sizeof(uint32_t), 1);

    // Perform write and read operations.
    remote_tt_device->write_to_device(data_to_write.data(), tensix_core_xy, test_address, test_size);
    remote_tt_device->wait_for_non_mmio_flush();
    remote_tt_device->read_from_device(data_read.data(), tensix_core_xy, test_address, test_size);

    // Verify data matches.
    ASSERT_EQ(data_to_write.size(), data_read.size()) << "Read and write data sizes do not match";
    for (size_t i = 0; i < data_to_write.size(); i++) {
        ASSERT_EQ(data_to_write[i], data_read[i]) << "Data mismatch at index " << i << ": expected 0x" << std::hex
                                                  << data_to_write[i] << " but got 0x" << data_read[i];
    }

    // Now write some random data.
    for (size_t i = 0; i < data_to_write.size(); i++) {
        data_to_write[i] = i;
    }
    remote_tt_device->write_to_device(data_to_write.data(), tensix_core_xy, test_address, test_size);
    remote_tt_device->wait_for_non_mmio_flush();
    remote_tt_device->read_from_device(data_read.data(), tensix_core_xy, test_address, test_size);

    // Verify data matches.
    ASSERT_EQ(data_to_write.size(), data_read.size()) << "Read and write data sizes do not match";
    for (size_t i = 0; i < data_to_write.size(); i++) {
        ASSERT_EQ(data_to_write[i], data_read[i]) << "Data mismatch at index " << i << ": expected 0x" << std::hex
                                                  << data_to_write[i] << " but got 0x" << data_read[i];
    }
}
