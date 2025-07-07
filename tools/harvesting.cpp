// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <cxxopts.hpp>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/cluster.h"

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

    auto print_cores = [&](chip_id_t chip, CoreType core_type) {
        std::string core_type_str = to_str(core_type);
        std::cout << "Printing cores of type " << core_type_str << std::endl;
        const tt_SocDescriptor& soc_desc = cluster->get_chip(chip)->get_soc_descriptor();
        const std::vector<CoreCoord>& cores = soc_desc.get_cores(core_type);
        for (const CoreCoord& core : cores) {
            std::cout << core_type_str << " core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_cores = soc_desc.get_harvested_cores(core_type);
        for (const CoreCoord& harvested_core : harvested_cores) {
            std::cout << "Harvested " << core_type_str << " core " << harvested_core.str() << std::endl;
        }
    };

    for (chip_id_t chip : cluster->get_target_device_ids()) {
        std::cout << "Chip " << chip << std::endl;
        HarvestingMasks harvesting_masks = cluster->get_cluster_description()->get_harvesting_masks(chip);

        std::cout << "Tensix harvesting mask 0x" << std::hex << harvesting_masks.tensix_harvesting_mask << std::endl;

        std::cout << "DRAM harvesting mask 0x" << std::hex << harvesting_masks.dram_harvesting_mask << std::endl;

        std::cout << "ETH harvesting mask 0x" << std::hex << harvesting_masks.eth_harvesting_mask << std::endl;

        std::cout << "PCIE harvesting mask 0x" << std::hex << harvesting_masks.pcie_harvesting_mask << std::endl;

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
