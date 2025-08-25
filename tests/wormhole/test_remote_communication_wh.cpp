// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

using namespace tt::umd;

constexpr uint32_t DRAM_BARRIER_BASE = 0;

TEST(RemoteCommunicationWormhole, BasicRemoteCommunicationIO) {
    const uint64_t address0 = 0x1000;
    const uint64_t address1 = 0x2000;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    chip_id_t mmio_chip_id = *cluster->get_target_mmio_device_ids().begin();
    LocalChip* local_chip = cluster->get_local_chip(mmio_chip_id);
    std::unique_ptr<RemoteCommunication> remote_comm =
        std::make_unique<RemoteCommunication>(local_chip->get_tt_device(), local_chip->get_sysmem_manager());
    remote_comm->set_remote_transfer_ethernet_cores(local_chip->get_soc_descriptor().get_eth_xy_pairs_for_channels(
        cluster->get_cluster_description()->get_active_eth_channels(mmio_chip_id), CoordSystem::TRANSLATED));

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

    for (chip_id_t remote_chip_id : cluster->get_target_remote_device_ids()) {
        eth_coord_t remote_eth_coord = cluster_desc->get_chip_locations().at(remote_chip_id);

        for (const CoreCoord& core : cluster->get_soc_descriptor(remote_chip_id).get_cores(CoreType::TENSIX)) {
            CoreCoord translated_core =
                cluster->get_soc_descriptor(remote_chip_id).translate_coord_to(core, CoordSystem::TRANSLATED);
            remote_comm->write_to_non_mmio(
                remote_eth_coord,
                translated_core,
                data_to_write.data(),
                address0,
                data_to_write.size() * sizeof(uint32_t));

            cluster->write_to_device(
                data_to_write.data(), data_to_write.size() * sizeof(uint32_t), remote_chip_id, core, address1);

            remote_comm->wait_for_non_mmio_flush();

            remote_comm->read_non_mmio(
                remote_eth_coord, translated_core, data_read.data(), address1, data_read.size() * sizeof(uint32_t));

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
