// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cxxopts.hpp>
#include <iomanip>
#include <ios>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

int main(int argc, char* argv[]) {
    cxxopts::Options options("harvesting", "Extract harvesting information.");

    options.add_options()("h,help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    auto print_core_formatted = [&](const CoreCoord& core) {
        std::cout << "| (" << std::setw(2) << std::setfill(' ') << core.x << ", " << std::setw(2) << std::setfill(' ')
                  << core.y << ", " << ::to_str(core.core_type) << ", " << ::to_str(core.coord_system) << ") ";
    };

    auto print_core_all_systems = [&](const SocDescriptor& soc_desc, const CoreCoord& core) {
        for (CoordSystem coord_system :
             {CoordSystem::NOC0, CoordSystem::TRANSLATED, CoordSystem::LOGICAL, CoordSystem::NOC1}) {
            try {
                print_core_formatted(soc_desc.translate_coord_to(core, coord_system));
            } catch (const std::runtime_error& _) {
                // The try catch is used to handle non existing coordinates in some coordinate systems. For example
                // LOGICAL coords don't exist for harvested cores. In these cases we will just skip printing them, and
                // print only the existing ones.
            }
        }
        std::cout << std::endl;
    };

    auto print_cores = [&](ChipId chip, CoreType core_type) {
        std::string core_type_str = to_str(core_type);
        std::cout << "Printing cores of type " << core_type_str << std::endl;
        const SocDescriptor& soc_desc = cluster->get_chip(chip)->get_soc_descriptor();
        const std::vector<CoreCoord>& cores = soc_desc.get_cores(core_type);
        for (const CoreCoord& core : cores) {
            print_core_all_systems(soc_desc, core);
        }

        std::cout << "Printing Harvested cores of type " << core_type_str << std::endl;
        const std::vector<CoreCoord>& harvested_cores = soc_desc.get_harvested_cores(core_type);
        for (const CoreCoord& harvested_core : harvested_cores) {
            print_core_all_systems(soc_desc, harvested_core);
        }
    };

    for (ChipId chip : cluster->get_target_device_ids()) {
        std::cout << "Chip " << chip << std::endl;
        HarvestingMasks harvesting_masks = cluster->get_cluster_description()->get_harvesting_masks(chip);

        std::cout << "Tensix harvesting mask 0x" << std::hex << harvesting_masks.tensix_harvesting_mask << std::endl;

        std::cout << "DRAM harvesting mask 0x" << std::hex << harvesting_masks.dram_harvesting_mask << std::endl;

        std::cout << "ETH harvesting mask 0x" << std::hex << harvesting_masks.eth_harvesting_mask << std::endl;

        std::cout << "PCIE harvesting mask 0x" << std::hex << harvesting_masks.pcie_harvesting_mask << std::endl;
        std::cout << std::dec << std::endl;

        print_cores(chip, CoreType::TENSIX);
        print_cores(chip, CoreType::ETH);
        print_cores(chip, CoreType::DRAM);
        print_cores(chip, CoreType::ARC);
        print_cores(chip, CoreType::PCIE);
        print_cores(chip, CoreType::L2CPU);
        print_cores(chip, CoreType::SECURITY);
        print_cores(chip, CoreType::ROUTER_ONLY);
    }

    return 0;
}
