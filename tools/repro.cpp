// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <ostream>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>
#include <vector>

#include "common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt::umd;

int main(int argc, char* argv[]) {
    NocIdSwitcher noc1(NocId::NOC1);
    auto [cd, devs] = TopologyDiscovery::discover();
    for (uint32_t i = 0; i < 1000; i++) {
        for (auto& [id, device] : devs) {
            device->set_power_state(i % 2 == 0);
            for (uint32_t i = 0; i < 100; i++) {
                volatile uint32_t reset_state = device->get_risc_reset_state(CoreCoord(1, 2));
                device->is_pcie_hung(reset_state);
                device->is_noc_hung(NocId::NOC1);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
