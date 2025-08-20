// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

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

    // print eth_cores_up
    // for (auto& eth_core : eth_cores_up) {
    //     std::cout << "eth_core: " << eth_core.x << ", " << eth_core.y << std::endl;
    // }

    std::cout << "eth_cores_up.size(): " << eth_cores_up.size() << std::endl;
    lite_fabric::launch_lite_fabric(local_chip.get(), eth_cores_up);
}
