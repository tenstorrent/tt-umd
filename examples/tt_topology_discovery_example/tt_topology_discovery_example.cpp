// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <map>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

int main(int argc, char* argv[]) {
    std::cout << "=== Topology Discovery Example ===" << std::endl;
    std::cout << "Starting topology discovery..." << std::endl;

    // Configure topology discovery options.
    TopologyDiscoveryOptions options;
    options.no_remote_discovery = false;  // Enable remote device discovery via Ethernet
    options.no_wait_for_eth_training = false;
    options.no_eth_firmware_strictness = false;

    // Discover the cluster topology.
    auto [cluster_desc, discovered_devices] = TopologyDiscovery::discover(options);

    if (!cluster_desc) {
        std::cerr << "Failed to discover cluster topology" << std::endl;
        return 1;
    }

    std::cout << "\n=== Cluster Topology ===" << std::endl;
    std::cout << "Total chips discovered: " << cluster_desc->get_number_of_chips() << std::endl;

    // Get all chips in the cluster.
    std::unordered_set<ChipId> all_chips = cluster_desc->get_all_chips();

    // Separate local (MMIO) and remote chips.
    std::vector<ChipId> mmio_chips;
    std::vector<ChipId> remote_chips;

    for (ChipId chip_id : all_chips) {
        if (cluster_desc->is_chip_mmio_capable(chip_id)) {
            mmio_chips.push_back(chip_id);
        } else {
            remote_chips.push_back(chip_id);
        }
    }

    std::cout << "MMIO-capable (local) chips: " << mmio_chips.size() << std::endl;
    std::cout << "Remote chips: " << remote_chips.size() << std::endl;

    // Create and initialize TTDevices for all discovered chips
    // Note: We need to create MMIO chips first, since remote chips depend on them.
    std::vector<ChipId> chips_to_construct = cluster_desc->get_chips_local_first(all_chips);
    std::map<ChipId, std::unique_ptr<TTDevice>> tt_devices;

    std::cout << "\n=== Creating TTDevices ===" << std::endl;

    for (ChipId chip_id : chips_to_construct) {
        if (cluster_desc->is_chip_mmio_capable(chip_id)) {
            // Create local device via PCIe.
            auto chip_to_mmio_map = cluster_desc->get_chips_with_mmio();
            int pci_device_num = chip_to_mmio_map.at(chip_id);

            std::cout << "\nCreating local TTDevice for chip " << chip_id << " (PCI device " << pci_device_num << ")"
                      << std::endl;

            tt_devices[chip_id] = TTDevice::create(pci_device_num);
            tt_devices[chip_id]->init_tt_device();

            std::cout << "  Architecture: "
                      << (tt_devices[chip_id]->get_arch() == tt::ARCH::WORMHOLE_B0 ? "Wormhole B0"
                          : tt_devices[chip_id]->get_arch() == tt::ARCH::BLACKHOLE ? "Blackhole"
                                                                                   : "Unknown")
                      << std::endl;
            std::cout << "  Clock: " << tt_devices[chip_id]->get_clock() << " MHz" << std::endl;
            std::cout << "  Board ID: 0x" << std::hex << tt_devices[chip_id]->get_board_id() << std::dec << std::endl;
            std::cout << "  Temperature: " << tt_devices[chip_id]->get_asic_temperature() << "°C" << std::endl;
        }
        // Note: Creating remote TTDevices requires additional setup with RemoteCommunication
        // which is outside the scope of this basic example
    }

    // Print ethernet connections information.
    std::cout << "\n=== Ethernet Connections ===" << std::endl;

    for (ChipId chip_id : all_chips) {
        auto connected_chips = cluster_desc->get_directly_connected_chips(chip_id);
        if (!connected_chips.empty()) {
            std::cout << "Chip " << chip_id << " is connected to: ";
            for (const auto& [connected_chip, eth_channels] : connected_chips) {
                std::cout << "chip " << connected_chip << " (via " << eth_channels.size() << " eth channel(s)) ";
            }
            std::cout << std::endl;
        }
    }

    // Demonstrate memory operations on local devices.
    std::cout << "\n=== Memory Operations on Local Devices ===" << std::endl;

    for (ChipId chip_id : mmio_chips) {
        auto& device = tt_devices[chip_id];
        if (!device) {
            continue;
        }

        ChipInfo chip_info = device->get_chip_info();
        SocDescriptor soc_desc(device->get_arch(), chip_info);

        const std::vector<CoreCoord>& tensix_cores = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
        if (tensix_cores.empty()) {
            std::cout << "Chip " << chip_id << ": No Tensix cores available" << std::endl;
            continue;
        }

        CoreCoord tensix_core = tensix_cores[0];
        std::cout << "\nChip " << chip_id << " - Testing memory on core " << tensix_core.str() << std::endl;

        uint32_t test_data = 0xDEADBEEF;
        uint32_t read_data = 0;
        uint64_t mem_addr = 0x0;

        device->write_to_device(&test_data, tensix_core, mem_addr, sizeof(test_data));
        device->read_from_device(&read_data, tensix_core, mem_addr, sizeof(read_data));

        std::cout << "  Wrote: 0x" << std::hex << test_data << ", Read: 0x" << read_data << std::dec << std::endl;

        if (test_data == read_data) {
            std::cout << "  Memory test PASSED" << std::endl;
        } else {
            std::cout << "  Memory test FAILED" << std::endl;
        }
    }

    // Print chip location information.
    std::cout << "\n=== Chip Locations ===" << std::endl;
    auto chip_locations = cluster_desc->get_chip_locations();
    for (const auto& [chip_id, eth_coord] : chip_locations) {
        std::cout << "Chip " << chip_id << " location: rack=" << eth_coord.rack << ", shelf=" << eth_coord.shelf
                  << ", y=" << eth_coord.y << ", x=" << eth_coord.x << std::endl;
    }

    std::cout << "\n=== Topology Discovery Example Complete ===" << std::endl;
    return 0;
}
