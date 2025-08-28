// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/l1_address_map.h"
#include "umd/device/cluster.h"
#include "umd/device/lite_fabric/lite_fabric.h"
#include "umd/device/lite_fabric/lite_fabric_host_utils.h"
#include "umd/device/types/blackhole_eth.h"

using namespace tt::umd;

TEST(TestLiteFabric, LiteFabricInit) {
    std::vector<int> pci_devices_ids = PCIDevice::enumerate_devices();

    auto local_chip = LocalChip::create(pci_devices_ids[0]);

    auto eth_cores = local_chip->get_soc_descriptor().get_cores(CoreType::ETH);
    // 0x7CC04
    std::vector<CoreCoord> eth_cores_up;
    for (auto& eth_core : eth_cores) {
        uint32_t port_status;
        local_chip->read_from_device_reg(eth_core, &port_status, 0x7CC04, sizeof(port_status));

        if (port_status == blackhole::PORT_UP) {
            eth_cores_up.push_back(eth_core);
        }
    }

    local_chip->set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 0});

    lite_fabric::launch_lite_fabric(local_chip.get(), eth_cores_up);

    lite_fabric::set_chip(local_chip.get());

    auto host_interface = lite_fabric::LiteFabricMemoryMap::make_host_interface();

    uint32_t test_value = 0xdeadbeef;
    uint32_t test_addr = 0x1000;
    tt_xy_pair umd_core = local_chip->get_soc_descriptor().translate_coord_to(eth_cores_up[0], CoordSystem::TRANSLATED);

    tt_xy_pair target_tensix = {1, 2};
    uint64_t target_noc_addr = (uint64_t(target_tensix.y) << (36 + 6)) | (uint64_t(target_tensix.x) << 36) | test_addr;

    host_interface.write_any_len(&test_value, sizeof(test_value), eth_cores_up[0], target_noc_addr);

    uint32_t fabric_readback = 0;
    host_interface.read(&fabric_readback, sizeof(fabric_readback), eth_cores_up[0], target_noc_addr);
    std::cout << "fabric_readback: " << std::hex << fabric_readback << std::dec << std::endl;

    auto local_chip_2 = LocalChip::create(pci_devices_ids[1]);

    CoreCoord tensix_core = {target_tensix.x, target_tensix.y, CoreType::TENSIX, CoordSystem::TRANSLATED};
    uint32_t readback = 0;
    local_chip_2->read_from_device(tensix_core, &readback, test_addr, sizeof(readback));

    std::cout << "readback: " << std::hex << readback << std::dec << std::endl;
}
